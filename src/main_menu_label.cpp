#include "game_addresses.h"
#include "main_menu_label.h"
#include "runtime_arm.h"
#include <array>
#include <cstdint>

namespace gba_address = seed::address::common;

namespace seed {
namespace {
constexpr unsigned kPasswordLabelFirstTile = 0xC4;
// Each original menu label is three 1D objects: 32x16, 32x16, 8x16.
unsigned menu_label_tile_at(unsigned base, unsigned x, unsigned y) {
    const auto group = x / 32;
    return base + group * 8 + (y / 8) * (group == 2 ? 1 : 4) + (x % 32) / 8;
}
std::uint8_t read_menu_label_pixel(unsigned base, unsigned x, unsigned y) {
    const auto value = bus_read_u8(gba_address::kObjectTilesBegin +
                                   menu_label_tile_at(base, x, y) * 32 + (y % 8) * 4 + (x % 8) / 2);
    return (value >> (4 * (x & 1))) & 15;
}
} // namespace
void rename_local_vs_menu_item() {
    // Wait for the verified US menu graphics to load. All other scenes and
    // subsequent frames are left alone; the original ROM stays unchanged.
    std::uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 18 * 32; ++i)
        hash = (hash ^
                bus_read_u8(gba_address::kObjectTilesBegin + kPasswordLabelFirstTile * 32 + i)) *
               16777619u;
    if (hash != 0x77F138D2u)
        return;
    struct Glyph {
        unsigned label, x, width;
    };
    // L/O/A from NORMAL, C from the native SELECT footer, V/S from VS.
    // The footer's C uses the menu labels' outline and row-gradient colors.
    constexpr std::array<Glyph, 8> text{{{0x34, 50, 7},
                                         {0x34, 22, 8},
                                         {0x18A, 24, 7},
                                         {0x34, 43, 8},
                                         {0x34, 50, 7},
                                         {0, 0, 4},
                                         {0x46, 27, 9},
                                         {0x46, 35, 7}}};
    std::array<std::array<std::uint8_t, 72>, 16> image{};
    unsigned x = 10;
    for (const auto glyph : text) {
        if (!glyph.label) {
            x += glyph.width;
            continue;
        }
        for (unsigned y = 0; y < 7; ++y)
            for (unsigned col = 0; col < glyph.width; ++col) {
                auto value = read_menu_label_pixel(glyph.label, glyph.x + col, y + 1);
                if (glyph.label == 0x18A) {
                    const auto sx = glyph.x + col;
                    const auto packed =
                        bus_read_u8(gba_address::kObjectTilesBegin + (glyph.label + sx / 8) * 32 +
                                    y * 4 + (sx % 8) / 2);
                    const auto source = (packed >> (4 * (sx & 1))) & 15;
                    value = source == 7 ? (y == 1   ? 4
                                           : y == 2 ? 5
                                           : y == 3 ? 6
                                                    : 7)
                            : source    ? 1
                                        : 0;
                }
                // Keep glyph strokes only. Cropped sprite cells also contain
                // neighboring letters' outlines and the row below the VS label.
                if (value > 1)
                    image[y + 3][x + col] = value;
            }
        x += glyph.width - 1;
    }
    // Rebuild one consistent outline around the composed word, rather than
    // carrying opaque edges and stray pixels from the source sprite cells.
    const auto strokes = image;
    for (unsigned y = 1; y < 15; ++y)
        for (unsigned col = 1; col < 71; ++col)
            if (!image[y][col]) {
                bool edge = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        edge |= strokes[y + dy][col + dx] > 1;
                if (edge)
                    image[y][col] = 1;
            }
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned col = 0; col < 72; col += 2) {
            const auto tile = menu_label_tile_at(kPasswordLabelFirstTile, col, y);
            bus_write_u8(gba_address::kObjectTilesBegin + tile * 32 + (y % 8) * 4 + (col % 8) / 2,
                         image[y][col] | (image[y][col + 1] << 4));
        }
}
} // namespace seed
