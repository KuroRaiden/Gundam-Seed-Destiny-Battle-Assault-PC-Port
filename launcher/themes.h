#pragma once
#include "imgui.h"
#include "theme_catalog.h"
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
LauncherTheme load_theme(const std::filesystem::path& folder, std::string_view id);
} // namespace seed::launcher
