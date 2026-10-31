#pragma once
#include "imgui.h"
#include <array>
#include <filesystem>
#include <string_view>
namespace seed::launcher {
enum ThemeColor {
    Window,
    Panel,
    Border,
    Frame,
    FrameHover,
    Button,
    ButtonHover,
    ButtonActive,
    Accent,
    Muted,
    Text,
    Selected,
    Primary,
    PrimaryHover,
    PrimaryActive,
    ColorCount
};
struct LauncherTheme {
    std::array<ImVec4, ColorCount> colors;
};
inline constexpr std::array<std::string_view, 3> kThemeIds{"default", "earth-alliance", "zaft"};
inline constexpr std::array<const char*, 3> kThemeNames{"Default", "Earth Alliance", "ZAFT"};
unsigned theme_index(std::string_view id);
LauncherTheme load_theme(const std::filesystem::path& folder, std::string_view id);
} // namespace seed::launcher
