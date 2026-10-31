#include "themes.h"
#include <fstream>
#include <string>
namespace seed::launcher {
namespace {
constexpr const char* keys[]{"window",      "panel",         "border",        "frame",
                             "frame_hover", "button",        "button_hover",  "button_active",
                             "accent",      "muted",         "text",          "selected",
                             "primary",     "primary_hover", "primary_active"};
constexpr const char* files[]{"Default.ini", "EarthAlliance.ini", "ZAFT.ini"};
constexpr unsigned defaults[3][ColorCount]{
    {0x0b0f16, 0x131821, 0x293342, 0x1a212e, 0x263342, 0x213040, 0x2e4d5e, 0x1f6170, 0x33cce0,
     0x8c9eb8, 0xe3ebf7, 0x1a5261, 0x1c879c, 0x26a6b8, 0x1a6e82},
    {0x4ac8f8, 0xb9e9fa, 0x0461a9, 0xa2c4e8, 0xc9e8f7, 0xa2c4e8, 0xc6eaf8, 0x83bfdf, 0x0461a9,
     0x204e6b, 0x062d4b, 0x73d7a5, 0x007540, 0x009c41, 0x005f34},
    {0xaa2a1f, 0xaa2a1f, 0xa1c3e8, 0x651c1a, 0x7c201b, 0x7c201b, 0x92281f, 0x045e96, 0xd7e6f7,
     0xf5d9d2, 0xfff5f0, 0x045e96, 0x045e96, 0x0580c4, 0x034b77}};
ImVec4 color(unsigned value) {
    return {float(value >> 16 & 255) / 255, float(value >> 8 & 255) / 255, float(value & 255) / 255,
            1};
}
} // namespace
unsigned theme_index(std::string_view id) {
    for (unsigned i = 0; i < kThemeIds.size(); ++i)
        if (kThemeIds[i] == id)
            return i;
    return 0;
}
LauncherTheme load_theme(const std::filesystem::path& folder, std::string_view id) {
    const auto index = theme_index(id);
    LauncherTheme result;
    for (unsigned i = 0; i < ColorCount; ++i)
        result.colors[i] = color(defaults[index][i]);
    const auto file = folder / "Themes" / files[index];
    std::error_code error;
    if (std::filesystem::file_size(file, error) > 8192 || error)
        return result;
    std::ifstream input(file);
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const auto key = line.substr(0, separator), value = line.substr(separator + 1);
        if (value.size() != 7 || value[0] != '#' ||
            value.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            continue;
        for (unsigned i = 0; i < ColorCount; ++i)
            if (key == keys[i])
                result.colors[i] = color(unsigned(std::stoul(value.substr(1), nullptr, 16)));
    }
    return result;
}
} // namespace seed::launcher
