#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace seed {
inline constexpr unsigned kMsPaletteColors = 48;
inline constexpr unsigned kMsPaletteBytes = kMsPaletteColors * 2;
struct CustomPalette {
    bool enabled = false;
    std::array<std::uint16_t, kMsPaletteColors> colors{};
    bool operator==(const CustomPalette&) const = default;
    bool valid() const {
        return std::all_of(colors.begin(), colors.end(),
                           [&](auto c) { return c <= 0x7fff && (enabled || c == 0); });
    }
};
inline void encode_palette(std::uint8_t* out, const CustomPalette& palette) {
    for (unsigned i = 0; i < kMsPaletteColors; ++i) {
        out[i * 2] = std::uint8_t(palette.colors[i]);
        out[i * 2 + 1] = std::uint8_t(palette.colors[i] >> 8);
    }
}
inline bool decode_palette(const std::uint8_t* in, unsigned enabled, CustomPalette& palette) {
    if (enabled > 1)
        return false;
    CustomPalette candidate;
    candidate.enabled = enabled != 0;
    for (unsigned i = 0; i < kMsPaletteColors; ++i)
        candidate.colors[i] = in[i * 2] | (std::uint16_t(in[i * 2 + 1]) << 8);
    if (!candidate.valid())
        return false;
    palette = candidate;
    return true;
}
} // namespace seed
