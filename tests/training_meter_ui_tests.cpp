#include "host_window.h"
#include "combat_host.h"
#include "training_frame_meter.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void key(SDL_Scancode code, bool repeat = false, bool down = true) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = code;
    event.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    event.key.repeat = repeat;
    require(SDL_PushEvent(&event), "Could not inject test keyboard event");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: seed_training_meter_ui_tests CAPTURE.png");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        auto& meter = seed::training_frame_meter();
        meter.configure(true);
        for (unsigned frame = 0; frame < 50; ++frame) {
            std::array<seed::FighterFrameObservation, 2> sample{};
            sample[0].valid = sample[1].valid = true;
            sample[0].attacking = frame < 40;
            sample[0].move = 1;
            sample[0].animation_frame = static_cast<std::uint16_t>(frame);
            sample[0].active = (frame >= 12 && frame < 15) || (frame >= 21 && frame < 24);
            sample[0].projectile = frame >= 12 && frame < 42;
            sample[1].hitstun = frame >= 12 && frame < 39;
            meter.observe(sample);
        }
        gbarecomp::HostWindow window;
        require(window.open(3, 240, 160, "Training F5 meter verification"),
                "Could not open window");
        std::vector<std::uint8_t> image(240 * 160 * 3, 30);
        window.present(image.data());
        key(SDL_SCANCODE_F5);
        auto event = window.pump();
        require(meter.visible() && !event.remap_pause && !event.toggle_pause && !event.load_slot &&
                    !event.save_slot,
                "F5 did not open the meter without pausing or loading a state");
        key(SDL_SCANCODE_F5, true);
        window.pump();
        require(meter.visible(), "F5 repeat toggled the meter");
        key(SDL_SCANCODE_F5, false, false);
        window.pump();
        require(meter.visible(), "F5 release toggled the meter");
        // ImGui measures an auto-sized window and builds its font texture on
        // the first visible frame; inspect the following settled presents.
        for (unsigned present = 0; present < 3; ++present)
            window.present(image.data());
        auto surface = seed::suspend_menu_host();
        seed::resume_menu_host();
        auto* pixels = SDL_RenderReadPixels(surface.renderer, nullptr);
        require(pixels != nullptr, "Could not capture meter rendering");
        const std::array<std::array<Uint8, 3>, 4> colors{
            {{41, 190, 116}, {246, 71, 80}, {24, 135, 222}, {251, 208, 55}}};
        std::array<unsigned, 4> matched{};
        for (int y = 0; y < pixels->h; ++y)
            for (int x = 0; x < pixels->w; ++x) {
                Uint8 red, green, blue, alpha;
                require(SDL_ReadSurfacePixel(pixels, x, y, &red, &green, &blue, &alpha),
                        "Could not read meter pixel");
                for (unsigned color = 0; color < colors.size(); ++color)
                    if (red == colors[color][0] && green == colors[color][1] &&
                        blue == colors[color][2])
                        ++matched[color];
            }
        require(SDL_SavePNG(pixels, argv[1]), "Could not save meter capture");
        SDL_DestroySurface(pixels);
        for (auto count : matched)
            require(count > 5, "A requested frame phase color is missing from the native UI");
        key(SDL_SCANCODE_F4);
        require(window.pump().remap_pause, "Offline controls did not pause");
        key(SDL_SCANCODE_F5);
        require(window.pump().remap_pause && meter.visible(), "F5 escaped the F4 controls overlay");
        window.present(image.data());
        key(SDL_SCANCODE_F4);
        require(!window.pump().remap_pause, "Controls did not resume");
        key(SDL_SCANCODE_F5);
        require(!window.pump().remap_pause && !meter.visible(),
                "F5 close paused the game or failed");
        meter.configure(false);
        key(SDL_SCANCODE_F5);
        window.pump();
        require(!meter.allowed() && !meter.visible(), "VS enabled the Training meter");
        window.set_network_session(true);
        meter.configure(true, true);
        key(SDL_SCANCODE_F5);
        window.pump();
        require(!meter.visible(), "Online enabled the Training meter");
        window.close();
        SDL_Quit();
        std::printf("PASS: native F5 open/close/repeat/release, no pause/state hotkey leak, F4 "
                    "isolation, VS/online gates, rendered phase pixels %u/%u/%u/%u.\n",
                    matched[0], matched[1], matched[2], matched[3]);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
