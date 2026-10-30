#include "settings.h"
#include "portable_paths.h"
#include "platform.h"
#include "fighter_outline_setting.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace seed::launcher {
namespace {
std::string stored_path(const std::string& value, const std::filesystem::path& folder) {
    if (value.empty())
        return value;
    const auto selected = std::filesystem::path(wide(value));
    if (selected.is_relative())
        return utf8(selected.lexically_normal().generic_wstring());
    const auto base =
        folder.empty() ? std::filesystem::current_path() : std::filesystem::absolute(folder);
    const auto relative = selected.lexically_normal().lexically_relative(base.lexically_normal());
    if (!relative.empty() && *relative.begin() != L"..")
        return utf8(relative.generic_wstring());
    return value;
}
} // namespace
void GameFile::verify(const std::filesystem::path& base_directory) {
    verified = false;
    if (path.empty()) {
        message = "Choose a file";
        return;
    }
    try {
        auto selected = std::filesystem::path(wide(path));
        if (selected.is_relative() && !base_directory.empty()) {
            selected = std::filesystem::absolute(base_directory / selected).lexically_normal();
            path = utf8(selected.wstring());
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(selected, ec);
        if (ec) {
            message = "File not found or unreadable";
            return;
        }
        const std::uintmax_t expected_size = kind == FileKind::US         ? 4194304
                                             : kind == FileKind::Japanese ? 8388608
                                                                          : 16384;
        if (size != expected_size) {
            message = kind == FileKind::Bios ? "Choose the extracted 16 KB BIOS, not a ZIP archive"
                                             : "This file has the wrong cartridge size";
            return;
        }
        const char* expected =
            kind == FileKind::US
                ? "d7952790a4b82b6d230a4953e4bfe48f6693ddd550f1cf0e7ace74bce95453f8"
            : kind == FileKind::Japanese
                ? "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb"
                : "300c20df6731a33952ded8c436f7f186d25d3492";
        verified = file_hash(selected, kind == FileKind::Bios) == expected;
        message = verified ? "Verified" : "Unsupported file revision";
    } catch (const std::exception& error) {
        message = error.what();
    }
}
void Settings::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string key, value;
        if (!(row >> key >> std::quoted(value)))
            continue;
        if (key == "us")
            files[0].path = value;
        else if (key == "japanese")
            files[1].path = value;
        else if (key == "bios")
            files[2].path = value;
        else if (key == "scale") {
            try {
                scale = std::clamp(std::stoi(value), 2, 6);
            } catch (...) {
                scale = 3;
            }
        }
    }
    const auto outlines = seed::load_fighter_outline_settings(path);
    fighter_outlines = outlines.enabled;
    fighter_outline_opacity = outlines.opacity;
    for (unsigned index = 0; index < files.size(); ++index) {
        if (files[index].path.empty())
            files[index].path = kPortableGamePaths[index];
        files[index].verify(seed::settings_root(path));
    }
}
bool Settings::save(const std::filesystem::path& path, std::string& error) const {
    const auto staging = path.parent_path() / L"launcher-settings.pending";
    std::ofstream output(staging, std::ios::trunc);
    if (!output) {
        error = "Cannot save settings in the launcher folder.";
        return false;
    }
    output << "us " << std::quoted(stored_path(files[0].path, seed::settings_root(path))) << '\n'
           << "japanese " << std::quoted(stored_path(files[1].path, seed::settings_root(path))) << '\n'
           << "bios " << std::quoted(stored_path(files[2].path, seed::settings_root(path))) << '\n'
           << "scale " << std::quoted(std::to_string(scale)) << '\n'
           << seed::kFighterOutlineSetting << ' ' << std::quoted(fighter_outlines ? "1" : "0")
           << '\n'
           << seed::kFighterOutlineOpacitySetting << ' '
           << std::quoted(std::to_string(std::clamp(fighter_outline_opacity, 0, 100))) << '\n';
    output.close();
    if (!output) {
        error = "Settings could not be written completely.";
        return false;
    }
    // MoveFileEx provides replacement on Windows, including an existing destination.
    return replace_settings_file(staging, path, error);
}
} // namespace seed::launcher
