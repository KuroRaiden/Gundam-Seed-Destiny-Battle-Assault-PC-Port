#pragma once
#include "ms_palette.h"
#include <filesystem>
#include <map>
#include <vector>
namespace seed {
struct MsColorPreview {
    unsigned width = 0, height = 0;
    std::array<std::array<std::uint16_t, kMsPaletteColors>, 2> originals{};
    std::vector<std::uint8_t> indices;
    std::vector<std::uint8_t>
    rgba(const std::array<std::uint16_t, kMsPaletteColors>& palette) const;
};
std::map<unsigned, MsColorPreview> load_ms_previews(const std::filesystem::path& path);
} // namespace seed
