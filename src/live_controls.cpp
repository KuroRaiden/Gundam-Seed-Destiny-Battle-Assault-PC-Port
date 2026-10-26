#include "live_controls.h"
namespace seed {
void LiveControls::load(const std::filesystem::path& file) {
    path = file;
    bindings.load(path);
}
bool LiveControls::apply(const ControlBindings& edited, std::string& error) {
    if (!edited.save(path, error))
        return false;
    bindings = edited;
    release_guard();
    return true;
}
void LiveControls::event(const SDL_Event& event, SDL_Window* window) {
    if (overlay_open || !window || SDL_GetKeyboardFocus() != window)
        return;
    const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                     (!event.key.windowID || event.key.windowID == SDL_GetWindowID(window));
    const bool pad = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    if (!key && !pad)
        return;
    for (unsigned p = 0; p < 2; ++p) {
        const auto& profile = bindings.players[p];
        for (unsigned bit = 0; bit < 10; ++bit) {
            bool pressed = key && profile.keyboard_enabled &&
                           profile.keyboard[bit] != SDL_SCANCODE_UNKNOWN &&
                           profile.keyboard[bit] == event.key.scancode;
            if (pad && profile.pad_slot >= 0 && devices.id(profile.pad_slot) == event.gbutton.which)
                for (const auto binding : profile.gamepad[bit])
                    pressed |= binding.kind == PadBindingKind::Button &&
                               binding.index == event.gbutton.button;
            if (pressed)
                frame_presses[p] |= std::uint16_t(1u << bit);
        }
    }
}
void LiveControls::sample(SDL_Window* window, std::uint16_t* players) {
    players[0] = players[1] = 0;
    devices.refresh();
    if (overlay_open || !window || SDL_GetKeyboardFocus() != window)
        return;
    for (unsigned p = 0; p < 2; ++p) {
        const auto& profile = bindings.players[p];
        // A down/up pair drained in one pump still counts for this frame.
        // Keep it for every consumer, including the borrowed JP module.
        const auto held =
            sample_controls(profile, SDL_GetKeyboardState(nullptr), devices.pad(profile.pad_slot)) |
            frame_presses[p];
        if (wait_for_release[p]) {
            if (!held)
                wait_for_release[p] = false;
        } else
            players[p] = held;
    }
}
} // namespace seed
