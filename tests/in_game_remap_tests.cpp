#include "host_window.h"
#include "combat_host.h"
#include "live_controls.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <filesystem>

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void key(SDL_Scancode code, bool repeat = false) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = code;
    event.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    event.key.repeat = repeat;
    require(SDL_PushEvent(&event), "Key injection failed");
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: seed_in_game_remap_tests SCRATCH_DIRECTORY");
        std::filesystem::create_directories(argv[1]);
        gbarecomp::HostWindow window;
        require(window.open(3, 240, 160, "F4 remapping verification"), "Host open failed");
        std::vector<std::uint8_t> image(240 * 160 * 3, 30);
        window.present(image.data());
        auto surface = seed::suspend_menu_host();
        seed::resume_menu_host();
        require(surface.inputs != nullptr, "Shared input owner missing");
        SDL_RaiseWindow(surface.window);
        const auto focus_deadline = SDL_GetTicks() + 30000;
        while (SDL_GetKeyboardFocus() != surface.window && SDL_GetTicks() < focus_deadline) {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        require(SDL_GetKeyboardFocus() == surface.window,
                "Remapping verification needs keyboard focus on its test window");
        surface.inputs->path = std::filesystem::path(argv[1]) / "controls.ini";
        // A quick press/release can both arrive before one native event pump.
        // Keep it visible to every sampler in that frame, then release it.
        for (auto& profile : surface.inputs->bindings.players)
            profile.pad_slot = -1;
        key(SDL_SCANCODE_RETURN);
        SDL_Event released{};
        released.type = SDL_EVENT_KEY_UP;
        released.key.scancode = SDL_SCANCODE_RETURN;
        released.key.key = SDLK_RETURN;
        SDL_PushEvent(&released);
        auto quick = window.pump();
        require(!(quick.keyinput & 8),
                "A quick Enter tap was lost before Training could see Start");
        std::uint16_t quick_players[2]{};
        seed::sample_host_players(quick_players);
        require((quick_players[0] & 8) && !quick_players[1],
                "The JP shared-input callback lost the quick Start pulse");
        require(window.pump().keyinput & 8, "The quick Start pulse stuck for another host frame");
        key(SDL_SCANCODE_F4);
        auto event = window.pump();
        require(event.remap_pause && !event.quit && !event.load_slot && !event.save_slot,
                "Offline F4 did not pause or leaked into slot 4");
        require(surface.inputs->overlay_open, "Overlay did not block shared local input");
        std::uint16_t players[2]{1023, 1023};
        seed::sample_host_players(players);
        require(!players[0] && !players[1], "Overlay passed gameplay inputs");
        window.present(image.data());
        key(SDL_SCANCODE_F4, true);
        require(window.pump().remap_pause, "F4 repeat closed the overlay");
        window.set_network_session(true);
        event = window.pump();
        require(!event.remap_pause && surface.inputs->overlay_open,
                "Online remapping paused or closed its overlay");
        window.present(image.data());
        window.set_network_session(false);
        require(window.pump().remap_pause, "Switching back to offline did not pause");
        key(SDL_SCANCODE_F4);
        event = window.pump();
        require(!event.remap_pause && !event.load_slot && !event.quit &&
                    !surface.inputs->overlay_open,
                "F4 close did not resume cleanly");
        key(SDL_SCANCODE_F4);
        require(window.pump().remap_pause, "Overlay did not reopen");
        key(SDL_SCANCODE_ESCAPE);
        event = window.pump();
        require(!event.remap_pause && !event.quit,
                "Esc closed the game instead of its controls overlay");
        auto changed = surface.inputs->bindings;
        changed.players[1].keyboard[0] = SDL_SCANCODE_Q;
        std::string error;
        require(surface.inputs->apply(changed, error), "Live binding save failed");
        seed::ControlBindings saved;
        require(saved.load(surface.inputs->path) && saved == changed,
                "Live binding persistence failed");
        require(surface.inputs->wait_for_release[0] && surface.inputs->wait_for_release[1],
                "Closing/remapping did not guard held inputs");
        window.close();
        SDL_Quit();
        std::puts("PASS: quick Enter tap retention across shared samplers, F4/slot-4 isolation, "
                  "offline pause, online non-pause policy, input suppression, Esc resume, shared "
                  "live bindings and persistence.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
