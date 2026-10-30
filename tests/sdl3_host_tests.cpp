#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "brand_icons.h"
#include "host_window.h"
#include "combat_host.h"
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
std::string display_case = "windowed";
SDL_FRect expected_rectangle{};
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
    int physical_w = 0, physical_h = 0;
    require(SDL_GetRenderOutputSize(renderer, &physical_w, &physical_h),
            "Output size query failed");
    std::printf("Case %s: physical=%dx%d, overlay drawable=%dx%d rect=%d,%d %dx%d, SDL "
                "rect=%.1f,%.1f %.1fx%.1f\n",
                display_case.c_str(), physical_w, physical_h, out->drawable_width(),
                out->drawable_height(), layout.x, layout.y, layout.width, layout.height,
                expected_rectangle.x, expected_rectangle.y, expected_rectangle.w,
                expected_rectangle.h);
    require(out->drawable_width() == physical_w && out->drawable_height() == physical_h,
            "Overlay drawable differs from the physical output (letterbox offset lost)");
    require(layout.x == int(std::floor(expected_rectangle.x)) &&
                layout.y == int(std::floor(expected_rectangle.y)) &&
                layout.width == int(expected_rectangle.w) &&
                layout.height == int(expected_rectangle.h),
            "Overlay is not aligned to SDL's native game rectangle");
    unsigned checked = 0;
    for (int y = 0; y < 160; y += 3)
        for (int x = 0; x < 240; x += 3) {
            const auto p = pixels ? (*pixels)[y * 240 + x] : 0;
            Uint8 r, g, b, a;
            const int px = int(expected_rectangle.x + (x + .5) * expected_rectangle.w / 240);
            const int py = int(expected_rectangle.y + (y + .5) * expected_rectangle.h / 160);
            require(SDL_ReadSurfacePixel(surface, px, py, &r, &g, &b, &a),
                    "Capture pixel read failed");
            require(pixels ? r == (p & 31) * 255 / 31 && g == ((p >> 5) & 31) * 255 / 31 &&
                                 b == ((p >> 10) & 31) * 255 / 31
                           : r == 20 && g == 20 && b == 20,
                    "Renderer differs from expected framebuffer");
            ++checked;
        }
    // The RGB(20,20,20) guest image is deliberately distinguishable from every
    // RGB555 donor pixel. Scan the entire output, including the last row/column,
    // so an uncovered edge cannot hide between the sparse color samples.
    if (pixels) {
        for (int y = 0; y < surface->h; ++y)
            for (int x = 0; x < surface->w; ++x) {
                Uint8 r, g, b, a;
                require(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a), "Edge read failed");
                const bool inside = x + .5f >= expected_rectangle.x &&
                                    y + .5f >= expected_rectangle.y &&
                                    x + .5f < expected_rectangle.x + expected_rectangle.w &&
                                    y + .5f < expected_rectangle.y + expected_rectangle.h;
                require(!inside || r != 20 || g != 20 || b != 20,
                        "US framebuffer leaked through the JP selector");
                require(inside || (r == 7 && g == 11 && b == 20),
                        "JP selector painted into a letterbox bar");
            }
    }
    if (!capture.empty())
        require(SDL_SavePNG(surface,
                            (capture.parent_path() / (capture.stem().string() + "-" + display_case +
                                                      capture.extension().string()))
                                .string()
                                .c_str()),
                "Capture save failed");
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
        const auto stage_pack = std::filesystem::path(argv[1]).parent_path() / "destiny-stages.bin";
        const bool stages = std::filesystem::is_regular_file(stage_pack);
        if (stages)
            art.load_stages(stage_pack);
        seed::LocalVsSelection selection;
        selection.jp_layout = true;
        selection.seats = {{{13, 0, false}, {22, 7, false}}};
        pixels = &art.compose(selection);
        capture = argv[2];
        gbarecomp::HostWindow window;
        require(window.open(2, 240, 160, "SDL3 host validation"), "Native window failed");
#if defined(_WIN32)
        int icon_windows = 0;
        auto** icon_targets = SDL_GetWindows(&icon_windows);
        require(icon_targets && icon_windows == 1, "Expected one icon test window");
        const auto handle = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(icon_targets[0]), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        SDL_free(icon_targets);
        auto verify_icon = [&](const auto& expected, const char* label) {
            const auto icon =
                reinterpret_cast<HICON>(SendMessageW(handle, WM_GETICON, ICON_SMALL, 0));
            ICONINFO icon_info{};
            require(icon && GetIconInfo(icon, &icon_info), "Native window icon missing");
            BITMAP icon_bitmap{};
            require(GetObjectW(icon_info.hbmColor, sizeof(icon_bitmap), &icon_bitmap),
                    "Cannot read native window icon");
            require(icon_bitmap.bmWidth == seed::kBrandWindowIconSize &&
                        icon_bitmap.bmHeight == seed::kBrandWindowIconSize,
                    "Native icon dimensions differ from the requested original icon");
            BITMAPINFO icon_format{};
            icon_format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            icon_format.bmiHeader.biWidth = seed::kBrandWindowIconSize;
            icon_format.bmiHeader.biHeight = -int(seed::kBrandWindowIconSize);
            icon_format.bmiHeader.biPlanes = 1;
            icon_format.bmiHeader.biBitCount = 32;
            std::array<std::uint8_t, seed::kBrandWindowIconRgba.size()> icon_pixels{};
            const auto dc = GetDC(handle);
            const auto rows = GetDIBits(dc, icon_info.hbmColor, 0, seed::kBrandWindowIconSize,
                                        icon_pixels.data(), &icon_format, DIB_RGB_COLORS);
            ReleaseDC(handle, dc);
            DeleteObject(icon_info.hbmColor);
            DeleteObject(icon_info.hbmMask);
            require(rows == seed::kBrandWindowIconSize, "Could not read native icon pixels");
            unsigned branded_pixels = 0;
            for (unsigned pixel = 0; pixel < expected.size(); pixel += 4)
                if (expected[pixel + 3]) {
                    require(icon_pixels[pixel] == expected[pixel + 2] &&
                                icon_pixels[pixel + 1] == expected[pixel + 1] &&
                                icon_pixels[pixel + 2] == expected[pixel],
                            "Native window icon pixels differ from the original sprite");
                    ++branded_pixels;
                }
            std::printf("Verified %s window icon: %u original sprite pixels.\n", label,
                        branded_pixels);
        };
        verify_icon(seed::kBrandWindowIconRgba, "Haro game");
        auto** brand_windows = SDL_GetWindows(&icon_windows);
        require(brand_windows && icon_windows == 1, "Missing launcher icon test window");
        require(seed::set_launcher_window_icon(brand_windows[0]),
                "Could not set launcher ship icon");
        verify_icon(seed::kLauncherWindowIconRgba, "Ship launcher");
        require(seed::set_game_window_icon(brand_windows[0]), "Could not restore Haro game icon");
        SDL_free(brand_windows);
#endif
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
        auto native_surface = seed::suspend_menu_host();
        seed::resume_menu_host();
        require(SDL_GetRenderLogicalPresentationRect(native_surface.renderer, &expected_rectangle),
                "Logical game rectangle query failed");
        window.present(framebuffer.data());
        pixels = jp_pixels;
        window.present(framebuffer.data());
        require(captures == 2, "Host overlay was not presented");
        auto check_display = [&](const char* name, int width, int height, int fullscreen) {
            display_case = name;
            window.set_fullscreen(fullscreen);
            if (!fullscreen)
                require(SDL_SetWindowSize(native_surface.window, width, height),
                        "Window resize failed");
            require(SDL_SyncWindow(native_surface.window), "Window transition did not settle");
            window.pump();
            require(
                SDL_GetRenderLogicalPresentationRect(native_surface.renderer, &expected_rectangle),
                "Logical rectangle query failed");
            window.present(framebuffer.data());
        };
        check_display("wide", 1280, 720, 0);
        check_display("tall", 720, 1000, 0);
        check_display("fractional", 1279, 719, 0);
        check_display("fullscreen", 0, 0, 1);
        // Opening a host UI temporarily disables SDL logical presentation.
        // Its restoration must keep the selector aligned on the next frame.
        for (unsigned i = 0; i < 2; ++i) {
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.scancode = SDL_SCANCODE_F1;
            key.key.key = SDLK_F1;
            require(SDL_PushEvent(&key), "Movelist key injection failed");
            window.pump();
            window.present(framebuffer.data());
        }
        check_display("fullscreen-after-movelist", 0, 0, 1);
        check_display("fullscreen-exclusive", 0, 0, 2);
        check_display("windowed-again", 720, 480, 0);
        if (stages) {
            selection.stage_active = true;
            selection.stage_cursor = 17;
            pixels = &art.compose(selection);
            check_display("fullscreen-stage", 0, 0, 1);
            check_display("windowed-stage", 720, 480, 0);
        }
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
        std::puts("PASS: JP selector/stage alignment, wide/tall/fractional windows, fullscreen "
                  "transitions, "
                  "movelist restoration, audio startup/pause/resume/feed, independent virtual "
                  "gamepads, hot-unplug and quit.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
