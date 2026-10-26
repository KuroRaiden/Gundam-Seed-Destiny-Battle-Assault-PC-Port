#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "online_room.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using seed::NetworkEndpoint;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
unsigned be16(const unsigned char* p) {
    return (unsigned(p[0]) << 8) | p[1];
}
void put16(unsigned char* p, unsigned v) {
    p[0] = static_cast<unsigned char>(v >> 8);
    p[1] = static_cast<unsigned char>(v);
}
struct Socket {
    SOCKET s = INVALID_SOCKET;
    NetworkEndpoint ep;
    Socket() {
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        check(s != INVALID_SOCKET, "fixture socket");
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(!bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)), "fixture bind");
        int n = sizeof(a);
        getsockname(s, reinterpret_cast<sockaddr*>(&a), &n);
        ep = {a.sin_addr.s_addr, ntohs(a.sin_port)};
        u_long one = 1;
        ioctlsocket(s, FIONBIO, &one);
    }
    ~Socket() {
        closesocket(s);
    }
    void send(NetworkEndpoint to, const std::vector<unsigned char>& b) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = to.ipv4;
        a.sin_port = htons(to.port);
        sendto(s, reinterpret_cast<const char*>(b.data()), int(b.size()), 0,
               reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    template <class F> void drain(F action) {
        for (unsigned i = 0; i < 256; ++i) {
            std::vector<unsigned char> b(1500);
            sockaddr_in from{};
            int size = sizeof(from);
            int n = recvfrom(s, reinterpret_cast<char*>(b.data()), int(b.size()), 0,
                             reinterpret_cast<sockaddr*>(&from), &size);
            if (n < 0)
                break;
            b.resize(n);
            action(NetworkEndpoint{from.sin_addr.s_addr, ntohs(from.sin_port)}, b);
        }
    }
};
struct Fixture {
    Socket stun, punch, host_public, guest_public;
    NetworkEndpoint host{}, guest{};
    bool host_probe = false, guest_probe = false;
    unsigned blocked = 0, registrations = 0, queries = 0;
    seed::NatOptions options() {
        seed::NatOptions o;
        o.stun = {{"127.0.0.1", stun.ep.port}};
        o.autopunch = {"127.0.0.1", punch.ep.port};
        o.local_ipv4_override = htonl(0xCB007101);
        o.timeout_ms = 800;
        return o;
    }
    void mapping(NetworkEndpoint to, NetworkEndpoint peer, NetworkEndpoint external) {
        std::vector<unsigned char> b(8);
        put16(b.data(), peer.port);
        put16(b.data() + 2, external.port);
        std::memcpy(b.data() + 4, &external.ipv4, 4);
        punch.send(to, b);
    }
    void tick() {
        stun.drain([&](NetworkEndpoint from, const auto& b) {
            if (b.size() != 20 || be16(b.data()) != 1)
                return;
            if (!host)
                host = from;
            else if (from != host)
                guest = from;
            const auto mapped = from == host ? host_public.ep : guest_public.ep;
            std::vector<unsigned char> response(32);
            response[0] = 1;
            response[1] = 1;
            put16(response.data() + 2, 12);
            std::copy(b.begin() + 4, b.end(), response.begin() + 4);
            put16(response.data() + 20, 0x20);
            put16(response.data() + 22, 8);
            response[25] = 1;
            put16(response.data() + 26, mapped.port ^ 0x2112);
            const auto* ip = reinterpret_cast<const unsigned char*>(&mapped.ipv4);
            for (unsigned i = 0; i < 4; ++i)
                response[28 + i] = ip[i] ^ b[4 + i];
            stun.send(from, response);
        });
        punch.drain([&](NetworkEndpoint from, const auto& b) {
            if (b.size() == 1) {
                punch.send(from, b);
                return;
            }
            if (b.size() == 2) {
                ++registrations;
                check(be16(b.data()) == from.port, "registration not the gameplay socket");
                return;
            }
            if (b.size() != 8)
                return;
            ++queries;
            check(guest && from == guest && be16(b.data()) == guest.port &&
                      be16(b.data() + 6) == host.port,
                  "AutoPunch query wire format");
            mapping(guest, host, host_public.ep);
            mapping(host, guest, guest_public.ep);
        });
        // Two external sockets model distinct remapped ports. The gate discards
        // gameplay until BOTH peers have sent their own one-byte NAT probe.
        host_public.drain([&](NetworkEndpoint from, const auto& b) {
            if (!guest)
                guest = from;
            check(from == guest, "wrong external host source");
            if (b.size() == 1)
                guest_probe = true;
            if (host_probe && guest_probe)
                guest_public.send(host, b);
            else if (b.size() > 1)
                ++blocked;
        });
        guest_public.drain([&](NetworkEndpoint from, const auto& b) {
            check(from == host, "wrong external guest source");
            if (b.size() == 1)
                host_probe = true;
            if (host_probe && guest_probe)
                host_public.send(guest, b);
            else if (b.size() > 1)
                ++blocked;
        });
    }
};
int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    try {
        // STUN rejects a forged source, wrong transaction, truncation and bad
        // attribute length; only the correlated response may publish an endpoint.
        NetworkEndpoint server{htonl(INADDR_LOOPBACK), 19999};
        std::vector<unsigned char> request;
        seed::NatOptions opt;
        opt.stun = {{"127.0.0.1", server.port}};
        opt.autopunch = {};
        opt.timeout_ms = 500;
        seed::NatTraversal nat(
            12345,
            [&](NetworkEndpoint to, std::span<const unsigned char> b) {
                if (to == server)
                    request.assign(b.begin(), b.end());
                return true;
            },
            [](auto) {}, opt);
        for (unsigned i = 0; i < 100 && request.empty(); ++i) {
            nat.tick();
            SDL_Delay(2);
        }
        check(request.size() == 20, "STUN binding request");
        std::vector<unsigned char> response(32);
        response[0] = 1;
        response[1] = 1;
        put16(response.data() + 2, 12);
        std::copy(request.begin() + 4, request.end(), response.begin() + 4);
        put16(response.data() + 20, 0x20);
        put16(response.data() + 22, 8);
        response[25] = 1;
        put16(response.data() + 26, 45678 ^ 0x2112);
        const std::array<unsigned char, 4> ip{198, 51, 100, 9};
        for (unsigned i = 0; i < 4; ++i)
            response[28 + i] = ip[i] ^ request[4 + i];
        nat.receive({server.ipv4, 19998}, response);
        check(!nat.info().public_endpoint, "forged STUN source");
        response[8] ^= 1;
        nat.receive(server, response);
        check(!nat.info().public_endpoint, "wrong STUN transaction");
        response[8] ^= 1;
        nat.receive(server, {response.data(), response.size() - 1});
        check(!nat.info().public_endpoint, "truncated STUN response");
        response[23] = 99;
        nat.receive(server, response);
        check(!nat.info().public_endpoint, "bad STUN attribute");
        response[23] = 8;
        nat.receive(server, response);
        check(nat.info().public_endpoint.port == 45678 && !nat.info().searching,
              "correlated STUN result");
        Fixture fixture;
        seed::OnlineRoom host, guest;
        seed::LocalVsSelection left, right;
        left.jp_layout = right.jp_layout = true;
        left.available_fighters = right.available_fighters = 0x017fffff;
        host.host(0, "", fixture.options());
        auto start = SDL_GetTicks();
        while (host.code().empty()) {
            host.tick(left, 0);
            fixture.tick();
            SDL_Delay(2);
            check(SDL_GetTicks() - start < 2000, "automatic host code timeout");
        }
        auto code = seed::decode_join_code(host.code());
        check(code.internet && code.autopunch && code.public_endpoint == fixture.host_public.ep,
              "discovered host code");
        // An unauthenticated rendezvous mapping must receive only probes, never the
        // token-bearing control envelope from the listening host.
        Socket stranger;
        fixture.mapping(fixture.host, stranger.ep, stranger.ep);
        unsigned probes = 0;
        for (unsigned i = 0; i < 80; ++i) {
            host.tick(left, 0);
            fixture.tick();
            stranger.drain([&](auto, const auto& b) {
                check(b.size() == 1 && b[0] == 0, "room key leaked to rendezvous requester");
                ++probes;
            });
            SDL_Delay(2);
        }
        check(probes, "host NAT probe absent");
        guest.join(host.invitation(), fixture.options());
        start = SDL_GetTicks();
        while (!host.connected() || !guest.connected()) {
            host.tick(left, 0);
            guest.tick(right, 0);
            fixture.tick();
            SDL_Delay(2);
            check(SDL_GetTicks() - start < 4000, "two-way hole punch timed out");
        }
        check(fixture.host_probe && fixture.guest_probe && fixture.blocked && fixture.queries &&
                  fixture.registrations >= 2,
              "NAT gate was bypassed");
        check(host.connected() && guest.connected(), "room handshake failed after punch");
        std::puts("PASS: correlated STUN, malformed response rejection, gameplay-socket "
                  "registration, AutoPunch mappings, remapped ports, two-way filtered UDP "
                  "handshake and room-key protection.");
        WSACleanup();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        WSACleanup();
        return 1;
    }
}
