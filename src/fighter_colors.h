#pragma once
#include <cstdint>
namespace seed {
// JP palettes 0/1, plus both palettes for each hidden variant reached
// from portrait 0 (native fighters 26/27) and portrait 22 (fighter 24).
constexpr unsigned jp_color_count(std::uint8_t fighter) {
    return fighter == 0 ? 6 : fighter == 22 ? 4 : (fighter < 23 || fighter == 24) ? 2 : 0;
}
constexpr bool valid_jp_color(std::uint8_t fighter, std::uint8_t color) {
    return color < jp_color_count(fighter);
}
constexpr std::uint8_t jp_color_portrait(std::uint8_t fighter, std::uint8_t color) {
    return fighter == 0 && color >= 2    ? std::uint8_t(24 + color / 2)
           : fighter == 22 && color >= 2 ? 23
                                         : fighter;
}
struct JpFighterColor {
    std::uint8_t fighter; // One-based native battle ID, including hidden variants.
    std::uint8_t palette;
};
constexpr JpFighterColor jp_combat_choice(std::uint8_t fighter, std::uint8_t color) {
    return {std::uint8_t(jp_color_portrait(fighter, color) + 1), std::uint8_t(color & 1)};
}
} // namespace seed
