#pragma once
#include <array>
#include <cstdint>

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
    {0x08000860, 0x03000F44, 0x030004D0, 0x0300001C, 0x03000600,
     0x03000F6C, 0x030004C8, 0x030000A0, 0x03000070},
    {0x08000934, 0x03000F74, 0x0300060C, 0x030004EC, 0x03000F68,
     0x03000098, 0x030004CC, 0x030004E0, 0x03000F80},
}};
} // namespace seed
