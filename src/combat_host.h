#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace seed {
struct LiveControls;
// The menu runtime lends its existing surface to the combat module. Only one
// cartridge advances at a time; the owner retains its window, renderer and audio.
struct CombatSurface {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    LiveControls* inputs = nullptr;
    bool online = false;
};
void release_host_controls();
void sample_host_players(std::uint16_t* players);
CombatSurface suspend_menu_host();
void resume_menu_host();
void borrow_combat_surface(CombatSurface surface);
void combat_presentation_enabled(bool enabled);
void return_from_combat();
bool combat_window_closed();
} // namespace seed
