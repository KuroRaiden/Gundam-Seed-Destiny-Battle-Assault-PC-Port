#pragma once
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
namespace seed {
inline constexpr const char* kFighterOutlineSetting = "fighter_outlines";
inline constexpr const char* kFighterOutlineOpacitySetting = "fighter_outline_opacity";
struct FighterOutlineSettings {
    bool enabled = true;
    int opacity = 100;
};
// Shared by the launcher and JP host. Older settings keep solid outlines;
// malformed values are ignored and numeric opacity is clamped to 0..100.
inline FighterOutlineSettings load_fighter_outline_settings(const std::filesystem::path& path) {
    FighterOutlineSettings settings;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string key, value;
        if (!(row >> key >> std::quoted(value)))
            continue;
        if (key == kFighterOutlineSetting) {
            if (value == "0")
                settings.enabled = false;
            else if (value == "1")
                settings.enabled = true;
        } else if (key == kFighterOutlineOpacitySetting) {
            int parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size())
                settings.opacity = std::clamp(parsed, 0, 100);
        }
    }
    return settings;
}
inline bool load_fighter_outline_setting(const std::filesystem::path& path) {
    return load_fighter_outline_settings(path).enabled;
}
} // namespace seed
