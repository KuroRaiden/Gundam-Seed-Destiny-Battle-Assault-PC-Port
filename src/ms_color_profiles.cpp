#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ms_color_profiles.h"
#include <fstream>
#include <sstream>
namespace seed {
namespace {
bool key_valid(unsigned player, unsigned fighter, unsigned palette) {
    return player < 2 && fighter >= 1 && fighter <= 27 && palette < 2;
}
unsigned key(unsigned player, unsigned fighter, unsigned palette) {
    return (player * 27 + fighter - 1) * 2 + palette;
}
} // namespace
CustomPalette MsColorProfiles::get(unsigned player, unsigned fighter, unsigned palette) const {
    if (!key_valid(player, fighter, palette))
        return {};
    auto it = values_.find(key(player, fighter, palette));
    return it == values_.end() ? CustomPalette{} : it->second;
}
bool MsColorProfiles::set(unsigned player, unsigned fighter, unsigned palette,
                          const CustomPalette& value) {
    if (!key_valid(player, fighter, palette) || !value.valid())
        return false;
    const auto slot = key(player, fighter, palette);
    if (value.enabled)
        values_[slot] = value;
    else
        values_.erase(slot);
    return true;
}
bool MsColorProfiles::load(const std::filesystem::path& path, std::string& error) {
    values_.clear();
    error.clear();
    if (!std::filesystem::exists(path))
        return true;
    auto fail = [&] {
        error = "Invalid MS color profile. Reset or restore ms-colors.ini.";
        return false;
    };
    if (std::filesystem::file_size(path) > 65536)
        return fail();
    std::ifstream file(path);
    std::string header;
    if (!std::getline(file, header) || header != "SEEDPC_MS_COLORS 1")
        return fail();
    MsColorProfiles candidate;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        std::istringstream input(line);
        unsigned player, fighter, palette;
        if (!(input >> player >> fighter >> palette) || !key_valid(player, fighter, palette))
            return fail();
        CustomPalette value;
        value.enabled = true;
        unsigned color;
        for (auto& entry : value.colors) {
            if (!(input >> std::hex >> color) || color > 0x7fff)
                return fail();
            entry = std::uint16_t(color);
        }
        std::string extra;
        if ((input >> extra) || candidate.values_.contains(key(player, fighter, palette)))
            return fail();
        candidate.set(player, fighter, palette, value);
    }
    if (!file.eof())
        return fail();
    *this = std::move(candidate);
    return true;
}
bool MsColorProfiles::save(const std::filesystem::path& path, std::string& error) const {
    const auto pending = path.wstring() + L".pending";
    std::ofstream file(pending, std::ios::trunc);
    file << "SEEDPC_MS_COLORS 1\n";
    for (const auto& [slot, value] : values_) {
        file << std::dec << slot / 54 << ' ' << (slot % 54) / 2 + 1 << ' ' << slot % 2;
        for (auto color : value.colors)
            file << ' ' << std::hex << color;
        file << '\n';
    }
    file.close();
    if (!file || !MoveFileExW(pending.c_str(), path.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        error = "Could not save MS colors beside the launcher.";
        return false;
    }
    error.clear();
    return true;
}
} // namespace seed
