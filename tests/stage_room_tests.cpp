#include "online_room.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <chrono>
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        SDL_Init(0);
        seed::OnlineRoom host, guest;
        seed::LocalVsSelection left, right;
        for (auto* selection : {&left, &right}) {
            selection->jp_layout = selection->stage_enabled = true;
            selection->available_fighters = 0x017FFFFFu;
            selection->seats = {{{13, 0, false}, {22, 7, false}}};
        }
        host.host(0, "127.0.0.1");
        guest.join(host.invitation());
        const auto code = host.code();
        auto tick = [&](unsigned p1 = 0, unsigned p2 = 0) {
            host.tick(left, p1); guest.tick(right, p2);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        auto until = [&](auto ready, const char* error) {
            const auto began = SDL_GetTicks();
            while (!ready()) {
                tick();
                require(SDL_GetTicks() - began < 5000, error);
            }
        };
        until([&] {return host.connected() && guest.connected();}, "Room did not connect");
        auto select_fighters = [&] {
            tick(); tick(1,1); tick();
            until([&] {return left.stage_active && right.stage_active;}, "Stage screen did not synchronize");
            require(!host.launching() && !guest.launching(), "Characters launched before stage confirmation");
        };
        select_fighters();
        tick(0,16); tick();
        for (unsigned i = 0; i < 60; ++i) tick();
        require(left.stage_cursor == 0 && right.stage_cursor == 0, "Guest changed the host stage");
        tick(2); tick();
        until([&] {return !left.stage_active && !right.stage_active &&
                           !left.seats[1].ready && !right.seats[1].ready;}, "Host Back did not synchronize");
        select_fighters();
        tick(0,2); tick();
        until([&] {return !left.stage_active && !right.stage_active &&
                           !left.seats[1].ready && !right.seats[1].ready;}, "Guest Back did not synchronize");
        unsigned match = 0;
        for (const auto wanted : {24u, 7u, unsigned(seed::kRandomStage), 0u}) {
            select_fighters();
            for (unsigned step = 0; left.stage_cursor != wanted && step < 27; ++step) {
                tick(16); tick();
            }
            require(left.stage_cursor == wanted, "Host stage navigation failed");
            until([&] {return right.stage_cursor == wanted;}, "Stage preview did not reach guest");
            tick(1); tick();
            until([&] {return host.launching() && guest.launching();}, "Stage startup agreement timed out");
            require(seed::valid_jp_stage(left.stage) && left.stage == right.stage &&
                        (wanted == seed::kRandomStage || left.stage == wanted),
                    "Peers agreed on different stages");
            require(host.code() == code && guest.code() == code, "Stage rematch changed the code");
            const auto stage = left.stage;
            if (!match) {
                // Exercise the native-module retry acknowledgement, including
                // stage mismatches that must never be accepted across the handoff.
                const auto connection = guest.connection();
                seed::UdpTransport borrowed(connection);
                std::vector<std::uint8_t> offer(32 + 2 * seed::kMsPaletteBytes);
                auto put = [&](unsigned at, std::uint32_t value) {
                    for (unsigned byte = 0; byte < 4; ++byte)
                        offer[at + byte] = std::uint8_t(value >> (8 * byte));
                };
                put(0, seed::kRoomMagic); put(4, seed::kRoomProtocol);
                put(20, connection.epoch);
                offer[8] = 3; offer[9] = 0;
                offer[24] = 0; offer[25] = 7; offer[31] = 1;
                std::uint8_t fighters[2]{1, 8}, colors[2]{};
                seed::CustomPalette palettes[2];
                offer[30] = (stage + 1) % seed::kJpStageCount;
                require(!seed::acknowledge_room_start(borrowed, offer, connection, fighters,
                            colors, palettes, stage), "Wrong-stage offer was acknowledged");
                offer[30] = 25;
                require(!seed::acknowledge_room_start(borrowed, offer, connection, fighters,
                            colors, palettes, stage), "Invalid stage was acknowledged");
                offer[30] = stage;
                put(4, seed::kRoomProtocol - 65536);
                require(!seed::acknowledge_room_start(borrowed, offer, connection, fighters,
                            colors, palettes, stage), "Old-protocol stage offer was acknowledged");
                put(4, seed::kRoomProtocol);
                require(seed::acknowledge_room_start(borrowed, offer, connection, fighters,
                            colors, palettes, stage), "Agreed stage retry was not acknowledged");
            }
            host.match_returned(left, 12345 + match);
            guest.match_returned(right, 12345 + match);
            require(!left.stage_active && !right.stage_active, "Rematch retained a confirmed stage screen");
            require(left.stage_cursor == wanted && right.stage_cursor == wanted, "Rematch lost stage cursor");
            for (unsigned i = 0; i < 60; ++i) tick();
            std::printf("PASS: room match %u stage %u, same code\n", ++match, stage);
        }
        guest.close();
        until([&] {return host.failed();}, "Departure was not received");
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
