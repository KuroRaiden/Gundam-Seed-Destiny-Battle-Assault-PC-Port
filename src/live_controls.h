#pragma once
#include "input_bindings.h"

namespace seed {
// One input owner survives the US-menu / JP-combat handoff. Both modules edit
// and sample the same profiles and stable controller slots.
struct LiveControls {
    ControlBindings bindings;
    InputDevices devices;
    std::filesystem::path path;
    bool overlay_open = false;
    std::array<bool, 2> wait_for_release{};
    std::array<std::uint16_t, 2> frame_presses{};
    void load(const std::filesystem::path& file);
    bool apply(const ControlBindings& edited, std::string& error);
    void begin_frame() {
        frame_presses = {};
        devices.refresh();
    }
    void event(const SDL_Event& event, SDL_Window* window);
    void sample(SDL_Window* window, std::uint16_t* players);
    void release_guard() {
        wait_for_release = {true, true};
        frame_presses = {};
    }
};
} // namespace seed
