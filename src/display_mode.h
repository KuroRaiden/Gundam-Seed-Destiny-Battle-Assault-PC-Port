#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace seed {
// Tokens are the existing GBARecomp present-time ColorLut screen models.
inline constexpr std::array<const char*, 5> kDisplayModeIds{"raw", "frontlit", "unlit", "backlit",
                                                            "classic"};
inline constexpr std::array<const char*, 5> kDisplayModeNames{
    "Original colors", "GBA LCD (frontlit)", "GBA LCD (unlit)", "GBA SP (backlit)", "Classic GBA"};
inline unsigned display_mode_index(std::string_view id) {
    for (unsigned i = 0; i < kDisplayModeIds.size(); ++i)
        if (id == kDisplayModeIds[i])
            return i;
    return 0;
}
inline std::string load_display_mode(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string line, mode = "raw";
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string key, value;
        if (row >> key >> std::quoted(value) && key == "screen")
            mode = kDisplayModeIds[display_mode_index(value)];
    }
    return mode;
}
} // namespace seed
