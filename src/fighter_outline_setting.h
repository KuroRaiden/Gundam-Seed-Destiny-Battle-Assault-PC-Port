#pragma once
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
namespace seed {
inline constexpr const char* kFighterOutlineSetting = "fighter_outlines";
// Shared by the launcher and JP host. Older settings keep the existing on
// default; unrelated keys and malformed values never change this preference.
inline bool load_fighter_outline_setting(const std::filesystem::path& path) {
    bool enabled = true;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string key, value;
        if (!(row >> key >> std::quoted(value)) || key != kFighterOutlineSetting)
            continue;
        if (value == "0")
            enabled = false;
        else if (value == "1")
            enabled = true;
    }
    return enabled;
}
} // namespace seed
