#include "online_room.h"
#include "ms_palette_fixture.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <chrono>
void require(bool ok, const char* error) {
    if (!ok)
        throw std::runtime_error(error);
}
int main() {
    try {
        seed::OnlineRoom host, guest;
        seed::LocalVsSelection left, right;
        for (auto* selection : {&left, &right}) {
            selection->jp_layout = true;
            selection->available_fighters = 0x017FFFFF;
            selection->seats[0] = {13, 0, false};
            selection->seats[1] = {22, 7, false};
        }
        for (const auto invalid :
             {"", "seedpc://join?host=127.0.0.1&port=70000&key=00000000000000000000000000000000",
              "seedpc://join?host=bad/name&port=7000&key=00000000000000000000000000000000",
              "seedpc://join?host=localhost&port=7000junk&key=00000000000000000000000000000000"}) {
            bool rejected = false;
            try {
                guest.join(invalid);
            } catch (...) {
                rejected = true;
            }
            require(rejected && !guest.active(), "Malformed invitation was accepted");
        }
        left.seats[0].palette = ms_test_palette(0);
        right.seats[1].palette = ms_test_palette(1);
        host.host(0, "127.0.0.1", {}, "キラ");
        guest.join(host.invitation(), {}, "Athrun");
        auto tick = [&](unsigned p1 = 0, unsigned p2 = 0) {
            host.tick(left, std::uint16_t(p1));
            guest.tick(right, std::uint16_t(p2));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        const auto begin = SDL_GetTicks();
        while (!host.connected() || !guest.connected()) {
            tick();
            require(SDL_GetTicks() - begin < 3000, "Room handshake timed out");
        }
        require(host.names() == guest.names() && host.names()[0] == "キラ" &&
                    host.names()[1] == "Athrun",
                "Names differed between peers");
        require(left.seats[0].palette == right.seats[0].palette &&
                    left.seats[1].palette == right.seats[1].palette &&
                    left.seats[0].palette.enabled && left.seats[1].palette.enabled,
                "Custom palettes did not reach the opposing selector");
        require(host.seat() == 0 && guest.seat() == 1, "Room seats differ");
        tick(16, 0);
        tick(0, 0);
        for (unsigned i = 0; i < 70; ++i)
            tick();
        require(left.seats[0].cursor == right.seats[0].cursor && left.seats[0].cursor == 14,
                "Host selection did not reach guest");
        tick(0, 32);
        tick(0, 0);
        for (unsigned i = 0; i < 70; ++i)
            tick();
        require(left.seats[1].cursor == right.seats[1].cursor && left.seats[1].cursor == 21,
                "Guest did not control the separate Player 2 cursor");
        tick(32, 0); // Return to the base slot with two hidden variants.
        tick(0, 0);
        for (unsigned n = 0; n < 5; ++n) {
            tick(8, n == 0 ? 8 : 0);
            tick(0, 0);
        }
        for (unsigned i = 0; i < 70; ++i)
            tick();
        for (const auto* selection : {&left, &right})
            require(selection->seats[0].fighter == 0 && selection->seats[0].color == 5 &&
                        selection->seats[1].color == 1,
                    "Start colors/hidden variants did not reach both selectors");
        // Force the guest's ready packet to arrive before the host has sent
        // its own ready edge. The pending start must repeat that snapshot.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        guest.tick(right, 1);
        host.tick(left, 1);
        guest.tick(right, 0);
        require(guest.launching(),
                "Guest did not receive host readiness with the pending start offer");
        while (!host.launching() || !guest.launching()) {
            tick();
            require(SDL_GetTicks() - begin < 6000, "Room startup agreement timed out");
            if (guest.launching() && !host.launching()) {
                auto connection = guest.connection();
                seed::UdpTransport borrowed(connection);
                std::uint8_t fighters[2], colors[2];
                for (unsigned seat = 0; seat < 2; ++seat) {
                    const auto choice =
                        seed::jp_combat_choice(right.seats[seat].fighter, right.seats[seat].color);
                    fighters[seat] = choice.fighter;
                    colors[seat] = choice.palette;
                }
                for (const auto& p : borrowed.take_control())
                    seed::acknowledge_room_start(borrowed, p, connection, fighters, colors,
                                                 std::array<seed::CustomPalette, 2>{
                                                     right.seats[0].palette, right.seats[1].palette}
                                                     .data());
            }
        }
        const auto a = host.connection(), b = guest.connection();
        require(a.epoch && a.epoch == b.epoch && a.token == b.token,
                "Native combat contracts differ");
        require(a.local_port == b.peer_port && a.peer_port == b.local_port,
                "Socket handoff changed the endpoint");
        {
            seed::UdpTransport borrowed(a);
            require(borrowed.local_port() == a.local_port, "Borrowed socket was rebound");
        }
        {
            seed::UdpTransport borrowed(a);
            require(borrowed.local_port() == a.local_port,
                    "Borrow destructor closed the menu-owned socket");
        }
        {
            seed::UdpTransport borrowed(b);
            std::vector<std::uint8_t> offer(32 + 2 * seed::kMsPaletteBytes);
            auto put32 = [&](unsigned at, std::uint32_t value) {
                for (unsigned n = 0; n < 4; ++n)
                    offer[at + n] = std::uint8_t(value >> (8 * n));
            };
            put32(0, seed::kRoomMagic);
            put32(4, seed::kRoomProtocol);
            put32(20, b.epoch);
            offer[8] = 3;
            offer[9] = 0;
            offer[24] = 0;
            offer[25] = 6;
            offer[26] = 5;
            offer[27] = 1;
            const std::uint8_t fighters[]{27, 7}, colors[]{1, 1}, wrong_colors[]{0, 1};
            require(!seed::acknowledge_room_start(borrowed, offer, b, fighters, wrong_colors,
                                                  std::array<seed::CustomPalette, 2>{}.data()),
                    "Combat acknowledged a different palette contract");
            require(seed::acknowledge_room_start(borrowed, offer, b, fighters, colors,
                                                 std::array<seed::CustomPalette, 2>{}.data()),
                    "Hidden variant could not re-acknowledge a lost room offer");
            offer[27] = 2;
            require(!seed::acknowledge_room_start(borrowed, offer, b, fighters, colors,
                                                  std::array<seed::CustomPalette, 2>{}.data()),
                    "Invalid native palette was acknowledged");
        }
        host.match_returned(left, 1234);
        {
            seed::UdpTransport borrowed(b);
            for (unsigned i = 0; i < 25; ++i) {
                for (const auto& packet : borrowed.take_control())
                    require(packet.size() != 32 || packet[8] != 5,
                            "Successful match sent a competing room-leave notice");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        guest.match_returned(right, 1234);
        require(host.active() && guest.active() && host.connected() && guest.connected(),
                "Successful match closed the persistent room");
        require(left.seats[0].ready == false && right.seats[1].ready == false,
                "Returned players stayed ready");
        host.close();
        guest.close();
        left = {};
        right = {};
        for (auto* selection : {&left, &right}) {
            selection->jp_layout = true;
            selection->available_fighters = 0x017FFFFF;
            selection->seats[0] = {13, 0, false};
            selection->seats[1] = {22, 7, false};
        }
        host.host(0, "127.0.0.1");
        guest.join(host.code());
        auto cancellation_begin = SDL_GetTicks();
        while (!host.connected() || !guest.connected()) {
            tick();
            require(SDL_GetTicks() - cancellation_begin < 3000,
                    "Cancellation fixture handshake timed out");
        }
        host.close();
        while (!guest.failed()) {
            guest.tick(right, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            require(SDL_GetTicks() - cancellation_begin < 4000,
                    "Explicit cancellation did not notify peer");
        }
        require(guest.status() == "Opponent left the room.", "Cancellation reason changed");
        std::puts("PASS: invitation parsing, UDP room connection, independent selection, battle "
                  "agreement and borrowed socket lifetime.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
