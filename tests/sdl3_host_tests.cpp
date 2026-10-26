#include "host_window.h"
#include "host_overlay.h"
#include "jp_selection.h"
#include "jp_selection_overlay.h"
#include "controller_db.h"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}
const std::vector<std::uint16_t>* pixels = nullptr;
std::filesystem::path capture;
unsigned captures = 0;
void render(gbarecomp::HostOverlay* out) {
    if (pixels)
        seed::draw_jp_selection(out, *pixels);
    int count = 0;
    auto** windows = SDL_GetWindows(&count);
    require(windows && count == 1, "Expected one native test window");
    auto* renderer = SDL_GetRenderer(windows[0]);
    SDL_free(windows);
    auto* surface = SDL_RenderReadPixels(renderer, nullptr);
    require(surface != nullptr, "Renderer capture failed");
    const auto layout = out->game_rect();
    unsigned checked = 0;
    for (int y = 0; y < 160; y += 3)
        for (int x = 0; x < 240; x += 3) {
            const auto p = pixels ? (*pixels)[y * 240 + x] : 0;
            Uint8 r, g, b, a;
            const int px = layout.x + int((x + .5) * layout.width / 240);
            const int py = layout.y + int((y + .5) * layout.height / 160);
            require(SDL_ReadSurfacePixel(surface, px, py, &r, &g, &b, &a),
                    "Capture pixel read failed");
            require(pixels ? r == (p & 31) * 255 / 31 && g == ((p >> 5) & 31) * 255 / 31 &&
                                 b == ((p >> 10) & 31) * 255 / 31
                           : r == 20 && g == 20 && b == 20,
                    "Renderer differs from expected framebuffer");
            ++checked;
        }
    if (!capture.empty())
        require(SDL_SavePNG(surface, capture.string().c_str()), "Capture save failed");
    SDL_DestroySurface(surface);
    ++captures;
    std::printf("Renderer verified: %u sampled %s pixels, %dx%d output.\n", checked,
                pixels ? "JP selector" : "game texture", layout.width, layout.height);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "Usage: seed_sdl3_host_tests ART_PACK CAPTURE.png [--gpu]");
        if (argc == 3)
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        seed::JpSelectionArt art;
        art.load(argv[1]);
        seed::LocalVsSelection selection;
        selection.jp_layout = true;
        selection.seats = {{{13, 0, false}, {22, 7, false}}};
        pixels = &art.compose(selection);
        capture = argv[2];
        gbarecomp::HostWindow window;
        require(window.open(2, 240, 160, "SDL3 host validation"), "Native window failed");
        require(seed::load_controller_db().loaded, "Bundled controllerdb.txt was not loaded");
        // Direct audio deliberately starts paused until its startup buffer is
        // filled. Feed both paths before checking playback/pause controls.
        std::array<int16_t, 1097> samples{};
        for (unsigned i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>(2000 * std::sin(i * .04));
        for (unsigned i = 0; i < 40; ++i) {
            window.push_audio_block(samples.data(), samples.size(), 65536);
            SDL_Delay(2);
        }
        require(window.audio_enabled(), "SDL3 audio stream did not start");
        window.set_audio_enabled(false);
        require(!window.audio_enabled(), "Audio pause failed");
        window.set_audio_enabled(true);
        require(window.audio_enabled(), "Audio resume failed");
        window.set_host_overlay(render);
        std::vector<std::uint8_t> framebuffer(240 * 160 * 3, 20);
        const auto* jp_pixels = pixels;
        pixels = nullptr;
        window.present(framebuffer.data());
        pixels = jp_pixels;
        window.present(framebuffer.data());
        require(captures == 2, "Host overlay was not presented");
        window.set_host_overlay(nullptr);
        window.set_linear_filter(true);
        require(window.linear_filter(), "Linear filter toggle failed");
        window.set_linear_filter(false);
        require(window.audio_enabled(), "Audio stream stopped while feeding samples");
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "SEED validation pad";
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        const std::array<SDL_JoystickID, 2> ids{SDL_AttachVirtualJoystick(&desc),
                                                SDL_AttachVirtualJoystick(&desc)};
        require(ids[0] && ids[1] && ids[0] != ids[1], "Virtual gamepad IDs invalid");
        auto* first = SDL_OpenGamepad(ids[0]);
        auto* second = SDL_OpenGamepad(ids[1]);
        require(first && second, "Virtual gamepads failed to open");
        require(SDL_SetJoystickVirtualButton(SDL_GetGamepadJoystick(second),
                                             SDL_GAMEPAD_BUTTON_SOUTH, true),
                "Virtual P2 press failed");
        SDL_UpdateJoysticks();
        SDL_UpdateGamepads();
        require(!SDL_GetGamepadButton(first, SDL_GAMEPAD_BUTTON_SOUTH) &&
                    SDL_GetGamepadButton(second, SDL_GAMEPAD_BUTTON_SOUTH),
                "Two gamepad streams were mixed");
        SDL_CloseGamepad(first);
        require(SDL_DetachVirtualJoystick(ids[0]), "P1 unplug failed");
        SDL_UpdateGamepads();
        require(SDL_GetGamepadID(second) == ids[1] && SDL_GamepadConnected(second),
                "P1 disconnect changed P2's ID");
        SDL_CloseGamepad(second);
        require(SDL_DetachVirtualJoystick(ids[1]), "P2 unplug failed");
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        require(SDL_PushEvent(&quit), "Quit event injection failed");
        require(window.pump().quit, "SDL3 quit event was ignored");
        window.close();
        SDL_Quit();
        std::puts("PASS: SDL3 JP rendering, audio startup/pause/resume/feed, independent virtual "
                  "gamepads, hot-unplug and quit.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
