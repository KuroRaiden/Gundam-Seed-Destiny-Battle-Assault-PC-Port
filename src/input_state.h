#include "game_addresses.h"
#pragma once
#include <array>
#include <cstdint>

namespace us_address = seed::address::us;

namespace seed {
// Active-high GBA button bits. Two independent copies belong to the same match.
// Every member affects simulation and must be included in rollback snapshots.
struct InputState {
    std::uint16_t held = 0;
    std::uint16_t previous = 0;
    std::uint16_t pressed = 0;
    std::uint16_t released = 0;
    std::uint16_t repeat = 0;
    std::uint16_t recent_repress = 0;
    std::array<std::uint8_t, 10> repeat_ticks{};
    std::array<std::uint16_t, 16> history{};
    void advance(std::uint16_t buttons);
};
struct MatchInputs {
    std::array<InputState, 2> players{};
    void advance(std::uint16_t player1, std::uint16_t player2) {
        players[0].advance(player1);
        players[1].advance(player2);
    }
};
struct UsInputAddresses {
    std::uint32_t routine, held, previous, pressed, released, repeat;
    std::uint32_t recent_repress, repeat_ticks, history;
};
// Verified against the US ROM by IDA disassembly and the oracle test.
inline constexpr std::array<UsInputAddresses, 2> kUsInputAddresses{{
    {us_address::kPlayer1InputUpdate, us_address::kPlayer1Held, us_address::kPlayer1Previous,
     us_address::kPlayer1Pressed, us_address::kPlayer1Released, us_address::kPlayer1Repeat,
     us_address::kPlayer1RecentRepress, us_address::kPlayer1RepeatTicks,
     us_address::kPlayer1History},
    {us_address::kPlayer2InputUpdate, us_address::kPlayer2Held, us_address::kPlayer2Previous,
     us_address::kPlayer2Pressed, us_address::kPlayer2Released, us_address::kPlayer2Repeat,
     us_address::kPlayer2RecentRepress, us_address::kPlayer2RepeatTicks,
     us_address::kPlayer2History},
}};
} // namespace seed
