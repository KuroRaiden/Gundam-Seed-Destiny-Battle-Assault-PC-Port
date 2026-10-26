#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "udp_transport.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace seed {
namespace {
struct Winsock {
    Winsock() {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("Cannot initialize UDP networking");
    }
    ~Winsock() {
        WSACleanup();
    }
};
std::uint32_t u32(const std::uint8_t* b) {
    return b[0] | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
           (std::uint32_t(b[3]) << 24);
}
void put32(std::uint8_t* b, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        b[i] = std::uint8_t(n >> (8 * i));
}
bool same_peer(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family && a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}
sockaddr_in normalized(const sockaddr_in& input) {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = input.sin_port;
    out.sin_addr = input.sin_addr;
    return out;
}
} // namespace
struct UdpTransport::Impl {
    Winsock winsock;
    SOCKET socket = INVALID_SOCKET;
    bool owns_socket = true;
    std::uint32_t epoch = 0;
    sockaddr_in peer{};
    bool pinned = false, listening = true;
    std::vector<sockaddr_in> candidates;
    std::unique_ptr<NatTraversal> nat;
    void candidate(NetworkEndpoint endpoint) {
        if (pinned || listening || !endpoint || !valid_peer_ipv4(endpoint.ipv4))
            return;
        sockaddr_in value{};
        value.sin_family = AF_INET;
        value.sin_addr.s_addr = endpoint.ipv4;
        value.sin_port = htons(endpoint.port);
        if (candidates.size() < 8 &&
            std::none_of(candidates.begin(), candidates.end(),
                         [&](const auto& old) { return same_peer(old, value); }))
            candidates.push_back(value);
    }
    bool raw(NetworkEndpoint endpoint, std::span<const std::uint8_t> data) {
        if (data.size() > 1400 || !endpoint)
            return false;
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = endpoint.ipv4;
        to.sin_port = htons(endpoint.port);
        return sendto(socket, reinterpret_cast<const char*>(data.data()), int(data.size()), 0,
                      reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == int(data.size());
    }
    Token token{};
    std::uint16_t local = 0;
    Counters counters;
    std::deque<std::vector<std::uint8_t>> game, control;
    std::vector<GekkoNetResult*> results;
    GekkoNetAdapter adapter{};
    static thread_local Impl* active;
    ~Impl() {
        if (active == this)
            active = nullptr;
        if (owns_socket && socket != INVALID_SOCKET)
            closesocket(socket);
    }
    bool send(std::uint8_t kind, std::span<const std::uint8_t> data) {
        if (data.size() > 1368) {
            ++counters.rejected;
            return false;
        }
        if (!peer.sin_port && (listening || candidates.empty()))
            return false;
        std::vector<std::uint8_t> packet(32 + data.size());
        put32(packet.data(), 0x31554453u);
        packet[4] = 2;
        packet[5] = kind;
        packet[6] = std::uint8_t(data.size());
        packet[7] = std::uint8_t(data.size() >> 8);
        std::copy(token.begin(), token.end(), packet.begin() + 8);
        put32(packet.data() + 24, kind ? 0 : epoch);
        std::copy(data.begin(), data.end(), packet.begin() + 28);
        put32(packet.data() + packet.size() - 4, SDL_crc32(0, packet.data(), packet.size() - 4));
        bool sent = false;
        auto transmit = [&](const sockaddr_in& to) {
            if (raw({to.sin_addr.s_addr, ntohs(to.sin_port)}, packet)) {
                ++counters.sent;
                sent = true;
            }
        };
        if (pinned || candidates.empty())
            transmit(peer);
        else
            for (const auto& to : candidates)
                transmit(to);
        return sent;
    }
    void poll() {
        if (nat)
            nat->tick();
        for (unsigned i = 0; i < 512; ++i) {
            std::uint8_t packet[65536];
            sockaddr_in source{};
            int source_size = sizeof(source);
            const int count = recvfrom(socket, reinterpret_cast<char*>(packet), sizeof(packet), 0,
                                       reinterpret_cast<sockaddr*>(&source), &source_size);
            if (count == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                    break;
                if (error == WSAECONNRESET)
                    continue;
                break;
            }
            if (nat && source_size == sizeof(source) && source.sin_family == AF_INET &&
                nat->receive({source.sin_addr.s_addr, ntohs(source.sin_port)},
                             {packet, std::size_t(count)}))
                continue;
            bool valid = count >= 32 && count <= 1400 && source_size == sizeof(source) &&
                         source.sin_family == AF_INET;
            if (valid)
                valid = u32(packet) == 0x31554453u && packet[4] == 2 && packet[5] <= 1 &&
                        unsigned(packet[6] | (unsigned(packet[7]) << 8)) == unsigned(count - 32) &&
                        u32(packet + 24) == (packet[5] ? 0 : epoch) &&
                        std::equal(token.begin(), token.end(), packet + 8) &&
                        u32(packet + count - 4) == SDL_crc32(0, packet, count - 4);
            if (valid && pinned)
                valid = same_peer(peer, source);
            if (valid && !pinned) {
                // AutoPunch/STUN may reveal a different wire port. Only a
                // correct room token on a control packet can pin that endpoint.
                valid = packet[5] == 1 &&
                        (listening ||
                         std::any_of(candidates.begin(), candidates.end(), [&](const auto& c) {
                             return c.sin_addr.s_addr == source.sin_addr.s_addr;
                         }));
                if (valid) {
                    peer = normalized(source);
                    pinned = true;
                    if (nat)
                        nat->confirmed();
                }
            }
            if (!valid) {
                ++counters.rejected;
                continue;
            }
            ++counters.received;
            auto& queue = packet[5] ? control : game;
            if (queue.size() >= 512)
                queue.pop_front();
            queue.emplace_back(packet + 28, packet + count - 4);
        }
    }
    static void send_game(GekkoNetAddress* address, const char* data, int size) {
        if (!active || !address || address->size != sizeof(sockaddr_in) ||
            !same_peer(active->peer, *static_cast<sockaddr_in*>(address->data)) || size < 0)
            return;
        active->send(0, {reinterpret_cast<const std::uint8_t*>(data), std::size_t(size)});
    }
    static GekkoNetResult** receive_game(int* count) {
        *count = 0;
        if (!active)
            return nullptr;
        active->poll();
        active->results.clear();
        while (!active->game.empty()) {
            auto data = std::move(active->game.front());
            active->game.pop_front();
            auto* result = static_cast<GekkoNetResult*>(std::calloc(1, sizeof(GekkoNetResult)));
            if (!result)
                break;
            result->addr.data = std::malloc(sizeof(sockaddr_in));
            result->data = std::malloc(std::max<std::size_t>(1, data.size()));
            if (!result->addr.data || !result->data) {
                std::free(result->addr.data);
                std::free(result->data);
                std::free(result);
                break;
            }
            std::memcpy(result->addr.data, &active->peer, sizeof(sockaddr_in));
            result->addr.size = sizeof(sockaddr_in);
            std::memcpy(result->data, data.data(), data.size());
            result->data_len = unsigned(data.size());
            active->results.push_back(result);
        }
        *count = int(active->results.size());
        return active->results.data();
    }
};
thread_local UdpTransport::Impl* UdpTransport::Impl::active = nullptr;
UdpTransport::UdpTransport(std::uint16_t bind_port, const std::string& peer_host,
                           std::uint16_t peer_port, Token token)
    : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.token = token;
    p.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (p.socket == INVALID_SOCKET)
        throw std::runtime_error("Cannot create UDP socket");
    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(bind_port);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(p.socket, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) ==
        SOCKET_ERROR)
        throw std::runtime_error("Cannot bind UDP port " + std::to_string(bind_port));
    int length = sizeof(bind_address);
    getsockname(p.socket, reinterpret_cast<sockaddr*>(&bind_address), &length);
    p.local = ntohs(bind_address.sin_port);
    u_long nonblocking = 1;
    if (ioctlsocket(p.socket, FIONBIO, &nonblocking))
        throw std::runtime_error("Cannot configure UDP socket");
    if (peer_port) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        addrinfo* found = nullptr;
        if (getaddrinfo(peer_host.c_str(), std::to_string(peer_port).c_str(), &hints, &found))
            throw std::runtime_error("Cannot resolve opponent address");
        p.peer = normalized(*reinterpret_cast<sockaddr_in*>(found->ai_addr));
        freeaddrinfo(found);
        p.listening = false;
        p.candidates.push_back(p.peer);
    }
    p.adapter = {Impl::send_game, Impl::receive_game, std::free};
    activate();
}
UdpTransport::~UdpTransport() = default;
UdpTransport::UdpTransport(const NetConnection& borrowed) : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.owns_socket = false;
    p.socket = SOCKET(borrowed.socket);
    p.local = borrowed.local_port;
    p.peer.sin_family = AF_INET;
    p.peer.sin_addr.s_addr = borrowed.peer_ipv4;
    p.peer.sin_port = htons(borrowed.peer_port);
    p.token = borrowed.token;
    p.epoch = borrowed.epoch;
    p.pinned = true;
    p.listening = false;
    sockaddr_in local{};
    int size = sizeof(local);
    if (p.socket == INVALID_SOCKET || !borrowed.peer_port ||
        getsockname(p.socket, reinterpret_cast<sockaddr*>(&local), &size) ||
        ntohs(local.sin_port) != p.local)
        throw std::runtime_error("Online combat socket handoff is invalid");
    p.adapter = {Impl::send_game, Impl::receive_game, std::free};
    activate();
}
void UdpTransport::activate() {
    Impl::active = impl_.get();
}
GekkoNetAdapter* UdpTransport::adapter() {
    activate();
    return &impl_->adapter;
}
GekkoNetAddress UdpTransport::address() {
    return {&impl_->peer, sizeof(impl_->peer)};
}
void UdpTransport::poll() {
    activate();
    impl_->poll();
}
bool UdpTransport::send_control(std::span<const std::uint8_t> bytes) {
    return impl_->send(1, bytes);
}
std::vector<std::vector<std::uint8_t>> UdpTransport::take_control() {
    poll();
    std::vector<std::vector<std::uint8_t>> out;
    while (!impl_->control.empty()) {
        out.push_back(std::move(impl_->control.front()));
        impl_->control.pop_front();
    }
    return out;
}
std::uint16_t UdpTransport::local_port() const {
    return impl_->local;
}
std::uint16_t UdpTransport::peer_port() const {
    return ntohs(impl_->peer.sin_port);
}
std::string UdpTransport::peer_host() const {
    char text[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &impl_->peer.sin_addr, text, sizeof(text));
    return text;
}
UdpTransport::Counters UdpTransport::counters() const {
    return impl_->counters;
}
NetConnection UdpTransport::borrow(unsigned seat, std::uint32_t epoch) const {
    return {std::uint64_t(impl_->socket),
            impl_->peer.sin_addr.s_addr,
            epoch,
            impl_->local,
            ntohs(impl_->peer.sin_port),
            impl_->token,
            seat};
}
void UdpTransport::set_game_epoch(std::uint32_t epoch) {
    impl_->epoch = epoch;
    impl_->game.clear();
}
void UdpTransport::enable_nat(NatOptions options) {
    auto* p = impl_.get();
    p->nat = std::make_unique<NatTraversal>(
        p->local,
        [p](NetworkEndpoint to, std::span<const std::uint8_t> bytes) { return p->raw(to, bytes); },
        [p](NetworkEndpoint endpoint) { p->candidate(endpoint); }, std::move(options));
}
void UdpTransport::add_candidate(NetworkEndpoint endpoint) {
    impl_->candidate(endpoint);
}
void UdpTransport::punch_target(NetworkEndpoint endpoint, std::uint16_t internal) {
    if (impl_->nat)
        impl_->nat->target(endpoint, internal);
}
NatInfo UdpTransport::discovery() const {
    return impl_->nat ? impl_->nat->info() : NatInfo{};
}
} // namespace seed
