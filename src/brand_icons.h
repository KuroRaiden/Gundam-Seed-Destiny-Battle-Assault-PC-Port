#pragma once
#include "brand_window_icon.h"
#include <SDL3/SDL.h>

namespace seed {
inline bool set_window_brand(SDL_Window* window,
                             const std::array<std::uint8_t, 32 * 32 * 4>& rgba) {
    auto* surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32,
                                          const_cast<std::uint8_t*>(rgba.data()), 32 * 4);
    if (!surface)
        return false;
    const bool applied = SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
    return applied;
}
inline bool set_game_window_icon(SDL_Window* window) {
    return set_window_brand(window, kBrandWindowIconRgba);
}
inline bool set_launcher_window_icon(SDL_Window* window) {
    return set_window_brand(window, kLauncherWindowIconRgba);
}
} // namespace seed
