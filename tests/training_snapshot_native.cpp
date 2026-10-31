#include "jp_combat_runtime.h"
#include "training_snapshot.h"
#include "training_tools.h"
#include "training_snapshot_writer.h"
#include "training_meter_overlay.h"
#include "training_frame_meter.h"
#include "frame_control.h"
#include "fighter_outline.h"
#include "runtime_bus_bridge.h"
#include "gba_ppu.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <memory>
namespace {
std::filesystem::path output;
const char* renderer_name = "software";
void check(bool ok, const std::string& error) {
    if (!ok)
        throw std::runtime_error(error);
}
void png(const std::vector<std::uint8_t>& pixels, const std::filesystem::path& file) {
    SDL_Surface* image = SDL_CreateSurfaceFrom(240, 160, SDL_PIXELFORMAT_RGB24,
                                               const_cast<std::uint8_t*>(pixels.data()), 240 * 3);
    check(image, SDL_GetError());
    const bool ok = SDL_SavePNG(image, file.string().c_str());
    SDL_DestroySurface(image);
    check(ok, SDL_GetError());
}
std::vector<std::uint8_t> screen(SDL_Renderer* renderer) {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
    check(bool(surface), SDL_GetError());
    std::vector<std::uint8_t> rgb;
    for (int y = 0; y < surface->h; ++y)
        for (int x = 0; x < surface->w; ++x) {
            Uint8 r, g, b, a;
            check(SDL_ReadSurfacePixel(surface.get(), x, y, &r, &g, &b, &a), SDL_GetError());
            rgb.insert(rgb.end(), {r, g, b});
        }
    return rgb;
}
int drive(gbarecomp::NativeFrameControl& machine) {
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    auto advance = [&](unsigned frames, unsigned input = 0) {
        for (unsigned i = 0; i < frames; ++i) {
            seed::set_jp_frame_inputs(input, 0);
            check(machine.advance(1023 ^ input), "Training stopped");
            machine.drain_audio(audio);
        }
    };
    advance(600);
    check(seed::training_view().ready, "Training is not ready.");
    std::vector<std::uint8_t> before, expected, actual;
    std::string error;
    check(machine.save(before, error), error);
    auto* ppu = gbarecomp::active_ppu();
    check(ppu, "No PPU.");
    auto* observer = ppu->presentation_observer();
    ppu->set_presentation_observer(nullptr);
    advance(2);
    check(machine.save(expected, error), error);
    check(machine.load(before, error), error);
    ppu->set_presentation_observer(observer);
    SDL_Event snapshot_key{};
    snapshot_key.type = SDL_EVENT_KEY_DOWN;
    snapshot_key.key.scancode = SDL_SCANCODE_F7;
    check(seed::training_tools_event(snapshot_key, false), "F7 was not consumed.");
    check(machine.save(actual, error), error);
    check(actual == before, "Request changed native state.");
    advance(2);
    auto pixels = seed::take_training_snapshot();
    check(pixels.size() == 240 * 160 * 3, "No clean image.");
    check(machine.save(actual, error), error);
    check(actual == expected, "Snapshot mirror changed native state.");
    png(pixels, output / "clean.png");
    std::vector<std::uint8_t> native(machine.pixels(), machine.pixels() + pixels.size());
    png(native, output / "native.png");
    unsigned blue = 0;
    for (unsigned i = 0; i < 240 * 160; ++i)
        blue += pixels[i * 3] == 16 && pixels[i * 3 + 1] == 28 && pixels[i * 3 + 2] == 43;
    check(blue > 240 * 160 / 2, "Background was not replaced with the requested blue.");
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_F6;
    seed::training_tools_event(key, false);
    const auto* boxes = seed::training_snapshot_display_pixels(pixels.data());
    png(std::vector<std::uint8_t>(boxes, boxes + pixels.size()), output / "boxes.png");
    check(machine.save(actual, error), error);
    check(actual == expected, "Overlay changed native state.");
    snapshot_key.key.repeat = true;
    seed::training_tools_event(snapshot_key, false);
    snapshot_key.type = SDL_EVENT_KEY_UP;
    seed::training_tools_event(snapshot_key, false);
    advance(2);
    check(seed::take_training_snapshot().empty(), "F7 repeat or release captured again.");
    advance(24, 16);
    for (unsigned sample = 0; sample < 3; ++sample) {
        advance(2, sample == 1 ? 2 : 1);
        advance(4);
        check(seed::request_training_snapshot(), "Attack snapshot rejected.");
        advance(1);
        auto action = seed::take_training_snapshot();
        check(!action.empty(), "Attack snapshot did not finish in one frame.");
        png(action, output / ("attack-" + std::to_string(sample) + ".png"));
        advance(20);
    }
    advance(2, 8);
    advance(2);
    check(seed::training_view().paused, "Training pause did not open.");
    check(seed::request_training_snapshot(), "Paused snapshot rejected.");
    advance(2);
    pixels = seed::take_training_snapshot();
    check(!pixels.empty(), "Paused snapshot missing.");
    png(pixels, output / "paused.png");
    check(machine.save(expected, error), error);
    // Use the same PNG writer as HostWindow with a hidden real SDL renderer.
    // Verify ordinary, widescreen and portrait outputs, every overlay combination,
    // and restoration of the live target/letterbox/clip/draw state.
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("Training capture verification", 960, 640, SDL_WINDOW_HIDDEN);
    check(window, SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, renderer_name);
    check(renderer, SDL_GetError());
    {
        seed::TrainingMeterOverlay overlay;
        overlay.initialize(window, renderer);
        for (unsigned test = 0; test < 4; ++test) {
            const int width = test == 1 ? 1920 : test == 2 ? 721 : 960;
            const int height = test == 1 ? 1080 : test == 2 ? 1081 : 640;
            check(SDL_SetWindowSize(window, width, height), SDL_GetError());
            SDL_PumpEvents();
            const bool boxes_on = test & 1, meter_on = test & 2;
            if (seed::training_boxes_visible() != boxes_on)
                seed::training_tools_event(key, false);
            if (seed::training_frame_meter().visible() != meter_on)
                seed::training_frame_meter().toggle();
            for (unsigned warmup = 0; warmup < 3; ++warmup)
                overlay.draw(false);
            check(SDL_SetRenderLogicalPresentation(renderer, 240, 160,
                                                   SDL_LOGICAL_PRESENTATION_LETTERBOX),
                  SDL_GetError());
            SDL_SetRenderViewport(renderer, nullptr);
            const SDL_Rect clip{3, 4, 230, 150};
            SDL_SetRenderClipRect(renderer, &clip);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(renderer, 155, 33, 144, 211);
            SDL_RenderClear(renderer);
            const auto live_before = screen(renderer);
            const float scale = std::min(width / 240.f, height / 160.f);
            const SDL_FRect destination{(width - 240 * scale) / 2, (height - 160 * scale) / 2,
                                        240 * scale, 160 * scale};
            const auto folder = output / ("ui-" + std::to_string(test));
            check(seed::request_training_snapshot(), "Paused UI snapshot rejected.");
            check(seed::save_pending_training_snapshot(window, renderer, overlay, folder,
                                                       destination),
                  "Writer did not consume capture.");
            check(screen(renderer) == live_before, "Snapshot changed the live backbuffer.");
            SDL_Rect restored_clip{};
            SDL_GetRenderClipRect(renderer, &restored_clip);
            int lw, lh;
            SDL_RendererLogicalPresentation logical;
            SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &logical);
            check(lw == 240 && lh == 160 && logical == SDL_LOGICAL_PRESENTATION_LETTERBOX &&
                      SDL_RenderClipEnabled(renderer) && restored_clip.x == clip.x &&
                      restored_clip.y == clip.y && restored_clip.w == clip.w &&
                      restored_clip.h == clip.h && !SDL_GetRenderTarget(renderer),
                  "Writer did not restore renderer state.");
            unsigned saved = 0;
            for (const auto& file : std::filesystem::directory_iterator(folder)) {
                if (file.path().extension() != ".png")
                    continue;
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
                    SDL_LoadPNG(file.path().string().c_str()), SDL_DestroySurface);
                check(image && image->w == width && image->h == height,
                      "Saved PNG dimensions do not match physical window size.");
                std::filesystem::copy_file(file.path(),
                                           output / ("output-" + std::to_string(test) + ".png"),
                                           std::filesystem::copy_options::overwrite_existing);
                ++saved;
            }
            check(saved == 1, "PNG was not saved exactly once.");
            check(!seed::save_pending_training_snapshot(window, renderer, overlay, folder,
                                                        destination),
                  "Capture was saved twice.");
            SDL_SetRenderClipRect(renderer, nullptr);
            SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    check(machine.save(actual, error), error);
    check(actual == expected, "PNG/ImGui rendering changed native state.");
    // A restored paused state must not reuse an unrelated old sprite image.
    check(machine.save(before, error), error);
    check(machine.load(before, error), error);
    check(!seed::request_training_snapshot(), "Restore reused a stale paused image.");
    seed::configure_training_tools(false, false, output / "unused.sav");
    check(!seed::request_training_snapshot(), "Local VS allowed a Training snapshot.");
    seed::configure_training_tools(true, true, output / "unused.sav");
    check(!seed::request_training_snapshot(), "Online allowed a Training snapshot.");
    std::printf("PASS: clean Training capture, exact #101C2B, boxes, paused capture, unchanged "
                "native state, four PNG overlay/layout cases, live renderer restoration, "
                "VS/online exclusion.\n");
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5,
              "Pass JP ROM, BIOS, an E: output folder and optional SDL renderer.");
        if (argc == 5)
            renderer_name = argv[4];
        output = std::filesystem::u8path(argv[3]);
        std::filesystem::create_directories(output);
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.fighters[0] = 13;
        request.fighters[1] = 14;
        gbarecomp::RunOptions options;
        seed::configure_jp_combat(options, request);
        options.native_frame_driver = drive;
        std::vector<std::string> values{
            argv[0], "--rom",      argv[1],  "--bios",
            argv[2], "--bios-hle", "--save", (output / "oracle.sav").string()};
        std::vector<char*> args;
        for (auto& value : values)
            args.push_back(value.data());
        return gbarecomp::run_game(int(args.size()), args.data(), options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
