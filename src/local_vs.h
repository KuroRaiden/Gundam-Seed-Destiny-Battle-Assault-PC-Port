#pragma once
#include "input_state.h"
#include "fighter_colors.h"
#include "ms_palette.h"
#include <array>
#include <cstdint>

namespace seed {
enum class MenuBattleMode { None, Local, Online, Training };
// Qualify shared guest states by the last observed US menu choice.
struct VsMenuRoute {
    std::uint32_t previous_state = 0;
    MenuBattleMode selected = MenuBattleMode::None;
    MenuBattleMode observe(std::uint32_t state, std::uint8_t column, std::uint8_t row) {
        auto enter = MenuBattleMode::None;
        if (previous_state == 4 && state != 4) {
            if (selected == MenuBattleMode::Local && state == 7)
                enter = selected;
            if (selected == MenuBattleMode::Online && state == 12)
                enter = selected;
            if (selected == MenuBattleMode::Training && state == 11)
                enter = selected;
        }
        if (state == 4)
            selected = column == 2 && row == 2   ? MenuBattleMode::Local
                       : column == 1 && row == 0 ? MenuBattleMode::Online
                       : column == 2 && row == 0 ? MenuBattleMode::Training
                                                 : MenuBattleMode::None;
        previous_state = state;
        return enter;
    }
};
// US portrait order, including its random slot. Host-independent match state.
inline constexpr std::array<std::uint8_t, 13> kVsPortraits{8, 9, 0, 1, 2,  3, 255,
                                                           4, 5, 6, 7, 10, 11};
// Original Destiny 4 x 8 layout; only the middle row's two random cells
// are navigable. Other 255 cells are empty space around the VS graphic.
inline constexpr std::array<std::uint8_t, 32> kJpPortraits{
    14, 13, 19, 255, 255, 3, 4, 5, 24, 22, 12, 255, 255, 0,  1,  2,
    17, 16, 15, 255, 255, 6, 7, 8, 21, 20, 18, 255, 255, 11, 10, 9};
struct LocalVsSeat {
    std::uint8_t cursor = 2;
    std::uint8_t fighter = 0;
    bool ready = false;
    std::uint8_t color = 0; // Start cycles the original JP palettes/variants.
    CustomPalette palette;
};
struct LocalVsSelection {
    std::array<LocalVsSeat, 2> seats{{{2, 0, false}, {10, 7, false}}};
    std::uint32_t random = 0x53454544;
    std::uint16_t available_slots = 0x1FFF;
    std::uint32_t available_fighters = 0xFFF; // US battle implementations.
    CustomPalette (*palette_for)(unsigned seat, std::uint8_t fighter, std::uint8_t color) = nullptr;
    unsigned local_seat_mask = 3;
    bool jp_layout = false;
    bool training = false; // P1 chooses their fighter, then the practice opponent.
    unsigned ready_frames = 0;
    bool cancelled = false;
    bool launch = false;
    bool supported(std::uint8_t fighter) const {
        return fighter < 32 && (available_fighters & (1u << fighter));
    }
    std::uint8_t advance(const MatchInputs& inputs);
};
} // namespace seed
