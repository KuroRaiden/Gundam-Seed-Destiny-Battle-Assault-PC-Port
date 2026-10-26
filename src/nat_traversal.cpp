#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include "nat_traversal.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
namespace seed {
namespace {
std::uint64_t now() {
    return SDL_GetTicks();
}
unsigned be16(const std::uint8_t* p) {
    return (unsigned(p[0]) << 8) | p[1];
}
void put16(std::uint8_t* p, unsigned n) {
    p[0] = std::uint8_t(n >> 8);
    p[1] = std::uint8_t(n);
}
sockaddr_in address(NetworkEndpoint e) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = e.ipv4;
    a.sin_port = htons(e.port);
    return a;
}
std::vector<IP_ADAPTER_ADDRESSES*> adapters(std::vector<std::uint8_t>& storage) {
    ULONG size = 15000;
    storage.resize(size);
    auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    auto result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, first, &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, first, &size);
    }
    std::vector<IP_ADAPTER_ADDRESSES*> values;
    if (result != NO_ERROR)
        return values;
    for (auto* item = first; item; item = item->Next)
        if (item->OperStatus == IfOperStatusUp && item->IfType != IF_TYPE_SOFTWARE_LOOPBACK)
            values.push_back(item);
    return values;
}
std::uint32_t lan_address() {
    std::vector<std::uint8_t> data;
    const auto all = adapters(data);
    std::uint32_t fallback = 0;
    for (const auto* item : all)
        for (auto* unicast = item->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const auto ip =
                reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr.s_addr;
            if (!valid_peer_ipv4(ip))
                continue;
            if (item->FirstGatewayAddress)
                return ip;
            if (!fallback)
                fallback = ip;
        }
    return fallback;
}
std::uint32_t route_address(NetworkEndpoint endpoint) {
    const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET)
        return 0;
    const auto remote = address(endpoint);
    sockaddr_in local{};
    int size = sizeof(local);
    const bool ok =
        connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0 &&
        getsockname(socket, reinterpret_cast<sockaddr*>(&local), &size) == 0;
    closesocket(socket);
    return ok ? local.sin_addr.s_addr : 0;
}
std::vector<NetworkEndpoint> resolve(const DiscoveryServer& server) {
    std::vector<NetworkEndpoint> values;
    if (server.host.empty() || !server.port)
        return values;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* first = nullptr;
    if (getaddrinfo(server.host.c_str(), std::to_string(server.port).c_str(), &hints, &first))
        return values;
    for (auto* item = first; item && values.size() < 3; item = item->ai_next) {
        const auto* ip = reinterpret_cast<sockaddr_in*>(item->ai_addr);
        values.push_back({ip->sin_addr.s_addr, ntohs(ip->sin_port)});
    }
    freeaddrinfo(first);
    return values;
}
struct Resolution {
    std::atomic<bool> ready{false};
    std::vector<NetworkEndpoint> stun;
    NetworkEndpoint punch;
};
} // namespace
struct NatTraversal::Impl {
    struct Transaction {
        NetworkEndpoint server;
        std::array<std::uint8_t, 12> id{};
    };
    struct Punch {
        NetworkEndpoint endpoint;
        std::uint64_t expires = 0, last = 0;
    };
    std::uint16_t local_port;
    Send send;
    Mapping mapping;
    NatOptions options;
    NatInfo info;
    std::shared_ptr<Resolution> resolution = std::make_shared<Resolution>();
    std::vector<Transaction> transactions;
    std::vector<Punch> punches;
    NetworkEndpoint punch_server, target;
    std::uint16_t target_internal = 0;
    std::uint64_t began = now(), last_stun = 0, last_register = 0, last_query = 0;
    bool loaded = false, done = false;
    Impl(std::uint16_t port, Send output, Mapping mapped, NatOptions config)
        : local_port(port), send(std::move(output)), mapping(std::move(mapped)),
          options(std::move(config)) {
        info.lan_ipv4 = options.local_ipv4_override ? options.local_ipv4_override : lan_address();
        // DNS can block at the OS. Detached resolution owns only its result and
        // its own Winsock reference; cancelling a room never waits on DNS.
        std::thread([result = resolution, config = options] {
            WSADATA data;
            const bool initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
            try {
                if (initialized) {
                    for (const auto& server : config.stun) {
                        auto endpoints = resolve(server);
                        result->stun.insert(result->stun.end(), endpoints.begin(), endpoints.end());
                    }
                    auto punch = resolve(config.autopunch);
                    if (!punch.empty())
                        result->punch = punch.front();
                }
            } catch (...) {
            }
            if (initialized)
                WSACleanup();
            result->ready.store(true, std::memory_order_release);
        }).detach();
    }
    void tick() {
        if (done)
            return;
        const auto time = now();
        if (time - began >= options.timeout_ms)
            info.searching = false;
        if (!loaded && resolution->ready.load(std::memory_order_acquire)) {
            loaded = true;
            punch_server = resolution->punch;
            for (const auto endpoint : resolution->stun) {
                Transaction request;
                request.server = endpoint;
                if (BCryptGenRandom(nullptr, request.id.data(), unsigned(request.id.size()),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0)
                    transactions.push_back(request);
            }
            if (!options.local_ipv4_override && !transactions.empty()) {
                const auto ip = route_address(transactions.front().server);
                if (ip)
                    info.lan_ipv4 = ip;
            }
        }
        if (loaded && (last_stun == 0 || time - last_stun >= (info.public_endpoint ? 5000 : 700))) {
            for (const auto& request : transactions) {
                std::array<std::uint8_t, 20> bytes{};
                put16(bytes.data(), 1);
                bytes[4] = 0x21;
                bytes[5] = 0x12;
                bytes[6] = 0xA4;
                bytes[7] = 0x42;
                std::copy(request.id.begin(), request.id.end(), bytes.begin() + 8);
                send(request.server, bytes);
            }
            last_stun = time;
        }
        if (punch_server && (!last_register || time - last_register >= 1000)) {
            std::array<std::uint8_t, 2> registration{};
            put16(registration.data(), local_port);
            send(punch_server, registration);
            const std::uint8_t ping = 0;
            send(punch_server, {&ping, 1});
            last_register = time;
        }
        if (punch_server && target && (!last_query || time - last_query >= 400)) {
            std::array<std::uint8_t, 8> query{};
            put16(query.data(), local_port);
            std::memcpy(query.data() + 2, &target.ipv4, 4);
            put16(query.data() + 6, target_internal ? target_internal : target.port);
            send(punch_server, query);
            last_query = time;
        }
        std::erase_if(punches, [&](const auto& p) { return p.expires < time; });
        for (auto& punch : punches)
            if (!punch.last || time - punch.last >= 150) {
                const std::uint8_t zero = 0;
                send(punch.endpoint, {&zero, 1});
                punch.last = time;
            }
    }
    bool receive(NetworkEndpoint source, std::span<const std::uint8_t> bytes) {
        if (source == punch_server && punch_server) {
            if (bytes.size() == 1 && bytes[0] == 0) {
                info.autopunch_reachable = true;
                return true;
            }
            if (bytes.size() != 8 || done)
                return true;
            const auto internal = be16(bytes.data());
            NetworkEndpoint peer;
            peer.port = std::uint16_t(be16(bytes.data() + 2));
            std::memcpy(&peer.ipv4, bytes.data() + 4, 4);
            if (!internal || !peer || !valid_peer_ipv4(peer.ipv4) ||
                (target && (peer.ipv4 != target.ipv4 ||
                            (internal != target_internal && peer.port != target.port))))
                return true;
            auto found = std::find_if(punches.begin(), punches.end(),
                                      [&](const auto& p) { return p.endpoint == peer; });
            if (found == punches.end()) {
                if (punches.size() >= 8)
                    return true;
                punches.push_back({peer, now() + 5000, 0});
            } else
                found->expires = now() + 5000;
            // Only clients may add a token-bearing destination. A listening host
            // sends an unauthenticated one-byte punch, never its secret room key.
            if (target)
                mapping(peer);
            info.autopunch_reachable = true;
            return true;
        }
        if (bytes.size() == 1 && bytes[0] == 0)
            return true; // Peer NAT probe, not game data.
        const auto found = std::find_if(transactions.begin(), transactions.end(),
                                        [&](const auto& t) { return t.server == source; });
        if (found == transactions.end())
            return false;
        if (bytes.size() < 20 || bytes.size() > 512 || be16(bytes.data()) != 0x0101 ||
            be16(bytes.data() + 2) != bytes.size() - 20 || bytes[4] != 0x21 || bytes[5] != 0x12 ||
            bytes[6] != 0xA4 || bytes[7] != 0x42 ||
            !std::equal(found->id.begin(), found->id.end(), bytes.begin() + 8))
            return true;
        for (std::size_t at = 20; at + 4 <= bytes.size();) {
            const auto type = be16(bytes.data() + at), length = be16(bytes.data() + at + 2);
            at += 4;
            if (length > bytes.size() - at)
                return true;
            if ((type == 0x0020 || type == 1) && length == 8 && bytes[at] == 0 &&
                bytes[at + 1] == 1) {
                auto port = be16(bytes.data() + at + 2);
                std::array<std::uint8_t, 4> ip{};
                for (unsigned i = 0; i < 4; ++i)
                    ip[i] = bytes[at + 4 + i] ^ (type == 0x0020 ? bytes[4 + i] : 0);
                if (type == 0x0020)
                    port ^= 0x2112;
                NetworkEndpoint endpoint;
                std::memcpy(&endpoint.ipv4, ip.data(), 4);
                endpoint.port = std::uint16_t(port);
                if (endpoint && valid_peer_ipv4(endpoint.ipv4)) {
                    info.public_endpoint = endpoint;
                    info.searching = false;
                }
                return true;
            }
            at += (length + 3) & ~std::size_t(3);
        }
        return true;
    }
};
NatTraversal::NatTraversal(std::uint16_t port, Send send, Mapping mapping, NatOptions options)
    : impl_(std::make_unique<Impl>(port, std::move(send), std::move(mapping), std::move(options))) {
}
NatTraversal::~NatTraversal() = default;
void NatTraversal::target(NetworkEndpoint endpoint, std::uint16_t internal) {
    impl_->target = endpoint;
    impl_->target_internal = internal;
}
void NatTraversal::tick() {
    impl_->tick();
}
bool NatTraversal::receive(NetworkEndpoint source, std::span<const std::uint8_t> bytes) {
    return impl_->receive(source, bytes);
}
void NatTraversal::confirmed() {
    impl_->done = true;
    impl_->info.searching = false;
}
NatInfo NatTraversal::info() const {
    return impl_->info;
}
bool NatTraversal::same_lan(std::uint32_t peer) {
    if ((ntohl(peer) >> 24) == 127)
        return true;
    std::vector<std::uint8_t> data;
    for (const auto* item : adapters(data))
        for (auto* local = item->FirstUnicastAddress; local; local = local->Next) {
            if (local->Address.lpSockaddr->sa_family != AF_INET || !local->OnLinkPrefixLength ||
                local->OnLinkPrefixLength > 32)
                continue;
            const auto ip =
                reinterpret_cast<sockaddr_in*>(local->Address.lpSockaddr)->sin_addr.s_addr;
            const auto mask = local->OnLinkPrefixLength == 32
                                  ? 0xFFFFFFFFu
                                  : 0xFFFFFFFFu << (32 - local->OnLinkPrefixLength);
            if ((ntohl(ip) & mask) == (ntohl(peer) & mask))
                return true;
        }
    return false;
}
} // namespace seed
