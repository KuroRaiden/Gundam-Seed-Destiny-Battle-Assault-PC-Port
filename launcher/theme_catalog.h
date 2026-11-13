#pragma once
#include <array>
#include <string_view>

namespace seed::launcher {
enum ThemeIndex : unsigned { DefaultTheme, EarthAllianceTheme, ZaftTheme, BallTheme, CharTheme };
inline constexpr std::array<std::string_view, 5> kThemeIds{"default", "earth-alliance", "zaft",
                                                           "ball", "char"};
inline constexpr std::array<const char*, kThemeIds.size()> kThemeNames{"Default", "Earth Alliance",
                                                                       "ZAFT", "Ball", "Char"};

inline unsigned theme_index(std::string_view id) {
    for (unsigned i = 0; i < kThemeIds.size(); ++i)
        if (kThemeIds[i] == id)
            return i;
    return DefaultTheme;
}
} // namespace seed::launcher
