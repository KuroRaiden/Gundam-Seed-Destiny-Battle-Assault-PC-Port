#include "host_window.h"
#include "combat_host.h"
#include "move_list.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
void check(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
void key(SDL_Scancode code, bool repeat = false, bool down = true) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code;
    e.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    e.key.repeat = repeat;
    check(SDL_PushEvent(&e), "Event injection failed");
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide capture.png");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        seed::set_movelist_fighters({14, 27}, true);
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x1209/0x17ba");
        check(SDL_Init(SDL_INIT_GAMEPAD), "Gamepad init failed");
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.vendor_id = 0x1209;
        desc.product_id = 0x17ba;
        desc.name = "Turbo regression controller";
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        auto id = SDL_AttachVirtualJoystick(&desc);
        check(id, "Virtual pad attach failed");
        auto* pad = SDL_OpenGamepad(id);
        check(pad, "Virtual pad open failed");
        int pad_count = 0;
        auto* pads = SDL_GetGamepads(&pad_count);
        check(pad_count == 1 && pads[0] == id, "Test controller is not isolated");
        SDL_free(pads);
        gbarecomp::HostWindow window;
        check(window.open(4, 240, 160, "Movelist verification"), "Open failed");
        std::vector<std::uint8_t> frame(240 * 160 * 3, 30);
        window.present(frame.data());
        key(SDL_SCANCODE_F1);
        auto event = window.pump();
        check(event.remap_pause && !event.load_slot && !event.save_slot,
              "F1 failed to pause offline / leaked state key");
        key(SDL_SCANCODE_F1, true);
        key(SDL_SCANCODE_F1, false, false);
        check(window.pump().remap_pause, "F1 repeat/release closed movelist");
        for (unsigned i = 0; i < 3; ++i)
            window.present(frame.data());
        auto surface = seed::suspend_menu_host();
        seed::resume_menu_host();
        auto* pixels = SDL_RenderReadPixels(surface.renderer, nullptr);
        check(pixels && SDL_SavePNG(pixels, argv[1]), "Movelist capture failed");
        SDL_DestroySurface(pixels);
        key(SDL_SCANCODE_F4);
        check(window.pump().remap_pause, "F4 interrupted the open movelist");
        key(SDL_SCANCODE_ESCAPE);
        check(!window.pump().remap_pause, "Escape did not resume offline");
        key(SDL_SCANCODE_F4);
        check(window.pump().remap_pause, "F4 no longer pauses offline");
        key(SDL_SCANCODE_F1);
        event = window.pump();
        check(event.remap_pause && !event.load_slot, "F1 interrupted remapping or loaded a state");
        key(SDL_SCANCODE_F4);
        check(!window.pump().remap_pause, "F4 did not close controls");
        window.set_network_session(true);
        key(SDL_SCANCODE_F1);
        event = window.pump();
        check(!event.remap_pause && !event.toggle_pause && !event.load_slot,
              "F1 paused online / leaked state key");
        key(SDL_SCANCODE_F1);
        window.pump();
        check(SDL_SetJoystickVirtualAxis(SDL_GetGamepadJoystick(pad),
                                         SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767),
              "RT failed");
        SDL_UpdateJoysticks();
        SDL_UpdateGamepads();
        check(!window.pump().fast_forward, "Controller RT still triggers turbo");
        SDL_CloseGamepad(pad);
        SDL_DetachVirtualJoystick(id);
        window.close();
        SDL_Quit();
        std::puts("PASS: F1 open/close/repeat, offline pause, online continuation, reserved state "
                  "key, rendered glyphs and RT turbo regression.");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
