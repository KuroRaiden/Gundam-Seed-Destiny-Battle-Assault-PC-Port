#include "online_room.h"
#include "ms_palette_fixture.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void put32(std::vector<std::uint8_t>& p, unsigned at, std::uint32_t value) {
    for (unsigned n = 0; n < 4; ++n)
        p[at + n] = std::uint8_t(value >> (8 * n));
}
std::uint32_t get32(const std::vector<std::uint8_t>& p, unsigned at) {
    return p[at] | (std::uint32_t(p[at + 1]) << 8) | (std::uint32_t(p[at + 2]) << 16) |
           (std::uint32_t(p[at + 3]) << 24);
}
std::vector<std::uint8_t> control(unsigned kind, unsigned seat, std::uint32_t epoch) {
    std::vector<std::uint8_t> p(kind == 2 ? 32 + seed::kPlayerNameBytes + 1 + seed::kMsPaletteBytes
                                : kind == 3 || kind == 4 ? seed::kCombatControlPacketBytes
                                                         : 32);
    put32(p, 0, seed::kRoomMagic);
    put32(p, 4, seed::kRoomProtocol);
    p[8] = std::uint8_t(kind);
    p[9] = std::uint8_t(seat);
    put32(p, 20, epoch);
    return p;
}
} // namespace
int main() {
    try {
        require(seed::next_match_epoch(UINT32_MAX) == 1, "Match epoch wrap produced zero");
        seed::OnlineRoom host, guest;
        seed::LocalVsSelection left, right;
        for (auto* s : {&left, &right}) {
            s->jp_layout = true;
            s->available_fighters = 0x017fffff;
            s->seats = {{{13, 0, false}, {22, 7, false}}};
        }
        left.seats[0].palette = ms_test_palette(0);
        right.seats[1].palette = ms_test_palette(1);
        host.host(0, "127.0.0.1", {}, "Kira");
        const auto code = host.code(), link = host.invitation();
        guest.join(code, {}, "Athrun");
        auto tick = [&](unsigned a = 0, unsigned b = 0) {
            host.tick(left, std::uint16_t(a));
            guest.tick(right, std::uint16_t(b));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        auto until = [&](auto ready, const char* message) {
            const auto began = SDL_GetTicks();
            while (!ready()) {
                tick();
                require(SDL_GetTicks() - began < 4000, message);
            }
        };
        until([&] { return host.connected() && guest.connected(); }, "Initial handshake timed out");
        std::uint32_t prior_epoch = 0;
        for (unsigned match = 0; match < 64; ++match) {
            tick();
            tick(8, 8);
            tick(); // Colors may change between matches.
            tick(1, 1);
            until([&] { return host.launching() && guest.launching(); },
                  "Rematch startup timed out");
            const auto a = host.connection(), b = guest.connection();
            require(a.epoch && a.epoch == b.epoch && a.epoch != prior_epoch,
                    "A rematch reused a native epoch");
            require(host.code() == code && guest.code() == code && host.invitation() == link,
                    "A rematch changed the join code");
            require(host.names() == guest.names() && host.names()[0] == "Kira" &&
                        host.names()[1] == "Athrun",
                    "A rematch lost player names");
            require(left.seats[0].palette == right.seats[0].palette &&
                        left.seats[1].palette == right.seats[1].palette,
                    "A rematch lost custom palettes");
            prior_epoch = a.epoch;
            const auto crc = 0x12340000u + match;
            host.match_returned(left, crc);
            require(host.active() && !host.launching() &&
                        host.epoch() == seed::next_match_epoch(a.epoch),
                    "Early return closed or reused the room");
            // Hold A across return. An attack/confirm held at the KO must not
            // automatically confirm the next match before it is released.
            host.tick(left, 1);
            require(!left.seats[0].ready, "Held A automatically readied a rematch");
            if (match == 0) {
                seed::UdpTransport late(b);
                std::vector<std::uint8_t> done(seed::kCombatControlPacketBytes);
                put32(done, 0, seed::kCombatControlMagic);
                put32(done, 4, seed::kRoomProtocol);
                done[8] = 3;
                done[9] = 1;
                put32(done, 12, b.epoch);
                put32(done, 16, crc);
                // Repeated DONE emulates a lost final acknowledgement while
                // the host has already returned to selection.
                for (unsigned retry = 0; retry < 3; ++retry) {
                    late.send_control(done);
                    host.tick(left, 0);
                    bool ack = false;
                    for (const auto& p : late.take_control()) {
                        require(p.size() < 9 || p[8] != 5, "Successful return sent a leave notice");
                        ack |= p.size() == seed::kCombatControlPacketBytes &&
                               get32(p, 0) == seed::kCombatControlMagic && p[8] == 4 &&
                               get32(p, 12) == b.epoch && get32(p, 16) == crc;
                    }
                    require(ack, "Returned menu did not service the previous match's final ack");
                }
                put32(done, 16, crc + 1);
                late.send_control(done);
                host.tick(left, 0);
                for (const auto& p : late.take_control())
                    require(p.size() != seed::kCombatControlPacketBytes ||
                                get32(p, 0) != seed::kCombatControlMagic || p[8] != 4,
                            "Wrong final checksum was acknowledged");
            }
            guest.match_returned(right, crc);
            for (unsigned n = 0; n < 50; ++n)
                tick();
            // Delayed ready/offer/ack/leave packets from the completed match
            // must not alter a fresh selection or close an established rematch.
            {
                seed::UdpTransport old_guest(b), old_host(a);
                auto ready = control(2, 1, b.epoch);
                ready[10] = 22;
                ready[11] = 7;
                ready[12] = 1;
                put32(ready, 16, 0x7ffffff0u);
                std::memcpy(ready.data() + 32, "STALE", 6);
                old_guest.send_control(ready);
                old_guest.send_control(control(5, 1, b.epoch));
                old_host.send_control(control(3, 0, a.epoch));
                old_guest.send_control(control(4, 1, b.epoch));
                tick();
                require(host.connected() && guest.connected() && !host.launching() &&
                            !guest.launching(),
                        "Stale control traffic changed the rematch");
                require(!left.seats[1].ready && host.names()[1] == "Athrun",
                        "Old ready/name data reached the new selection");
                // The old native transport can still emit Gekko data on the
                // retained socket; selection's epoch-zero filter must drop it.
                auto address = old_guest.address();
                const char marker[] = "old-match";
                auto* adapter = old_guest.adapter();
                adapter->send_data(&address, marker, sizeof(marker));
                host.tick(left, 0);
                require(host.active(), "Old Gekko traffic closed the rematch");
            }
            require(host.completed_matches() == match + 1 && guest.completed_matches() == match + 1,
                    "Match count did not advance");
        }
        host.close();
        until([&] { return guest.failed(); },
              "Leaving the persistent room did not notify the peer");
        require(!guest.active() && guest.status() == "Opponent left the room.",
                "Explicit leave did not release the room");
        std::puts(
            "PASS: 64 rematches share one code/socket, preserve names/palettes, reject stale "
            "control/game packets, guard held input, re-ack late finishes and cleanly disconnect.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
