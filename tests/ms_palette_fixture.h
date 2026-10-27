#pragma once
#include "ms_palette.h"
inline seed::CustomPalette ms_test_palette(unsigned seat) {
    seed::CustomPalette palette;
    palette.enabled = true;
    for (unsigned i = 0; i < seed::kMsPaletteColors; ++i)
        palette.colors[i] = std::uint16_t((i * 137 + seat * 1193 + 123) & 0x7fff);
    return palette;
}
