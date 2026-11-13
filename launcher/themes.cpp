#include "themes.h"
#include <fstream>
#include <string>
namespace seed::launcher {
namespace {
constexpr const char* keys[]{"window",      "panel",         "border",        "frame",
                             "frame_hover", "button",        "button_hover",  "button_active",
                             "accent",      "muted",         "text",          "selected",
                             "primary",     "primary_hover", "primary_active"};
constexpr const char* files[kThemeIds.size()]{"Default.ini", "EarthAlliance.ini", "ZAFT.ini",
                                              "Ball.ini", "Char.ini"};
constexpr unsigned defaults[kThemeIds.size()][ColorCount]{
    {0x0b0f16, 0x131821, 0x293342, 0x1a212e, 0x263342, 0x213040, 0x2e4d5e, 0x1f6170, 0x33cce0,
     0x8c9eb8, 0xe3ebf7, 0x1a5261, 0x1c879c, 0x26a6b8, 0x1a6e82},
    {0x4ac8f8, 0xb9e9fa, 0x0461a9, 0xa2c4e8, 0xc9e8f7, 0xa2c4e8, 0xc6eaf8, 0x83bfdf, 0x0461a9,
     0x204e6b, 0x062d4b, 0x73d7a5, 0x007540, 0x009c41, 0x005f34},
    {0xffffff, 0xffffff, 0xa1c3e8, 0xe5edf7, 0xd6e6f7, 0xf4e0de, 0xedc4bf, 0xb3cee8, 0xaa2a1f,
     0x455b6d, 0x142b3e, 0xc5def1, 0xaa2a1f, 0xc33629, 0x8d2119},
    {0x1b242e, 0x273643, 0x4e6475, 0x314453, 0x3a5060, 0x344857, 0x415b6a, 0x385c4a, 0x91d7a0,
     0xb3c2cd, 0xf0f5f7, 0x304d42, 0xa34440, 0xaf4d45, 0x873632},
    {0x1c1116, 0x321c22, 0x85623d, 0x49252c, 0x5e3035, 0x602a33, 0x79353e, 0x7f3839, 0xffd56a,
     0xc9ad9c, 0xfff3d9, 0x743039, 0xa83e45, 0xb0474b, 0x8b3038}};
ImVec4 color(unsigned value) {
    return {float(value >> 16 & 255) / 255, float(value >> 8 & 255) / 255, float(value & 255) / 255,
            1};
}
} // namespace
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
