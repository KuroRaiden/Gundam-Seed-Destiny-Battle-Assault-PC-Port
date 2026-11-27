#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "replay_player.h"
#include "match_replay.h"
#include "jp_combat.h"
#include "live_controls.h"
#include "brand_icons.h"
#include "controller_db.h"
#include "live_matches.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace seed {
namespace {
int run_viewer(const MatchReplay& replay, const std::filesystem::path& file, const std::string& rom,
               const std::string& bios, int scale, const ReplayLaunchOptions& launch,
               LiveMatchesClient* live = nullptr, bool from_start = false) {
    const auto library = combat_library_path();
    if (!replay_build_compatible(replay, replay_file_hash(library)))
        throw std::runtime_error("This replay needs its original game build: " +
                                 replay.build_version);
    if (replay.bios_hash != replay_file_hash(std::filesystem::u8path(bios)))
        throw std::runtime_error("This replay needs the same BIOS it was recorded with.");
    if (!SDL_Init(launch.mp4.empty() ? SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD : 0))
        throw std::runtime_error(SDL_GetError());
    struct Owner {
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        HMODULE module = nullptr;
        ~Owner() {
            if (module)
                FreeLibrary(module);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            SDL_Quit();
        }
    } owner;
    if (launch.mp4.empty()) {
        scale = std::clamp(scale, 1, 10);
        owner.window = SDL_CreateWindow("Gundam SEED - Replay", 240 * scale, 160 * scale,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!owner.window)
            throw std::runtime_error(SDL_GetError());
        set_game_window_icon(owner.window);
        owner.renderer = SDL_CreateRenderer(owner.window, nullptr);
        if (!owner.renderer)
            owner.renderer = SDL_CreateRenderer(owner.window, "software");
        if (!owner.renderer)
            throw std::runtime_error(SDL_GetError());
    }
    const auto root = library.parent_path();
    LiveControls controls;
    if (launch.mp4.empty()) {
        load_controller_db(root);
        controls.load(root / "config/controls.ini");
    }
    owner.module = LoadLibraryW(library.c_str());
    const auto run =
        owner.module
            ? reinterpret_cast<RunJpCombat>(GetProcAddress(owner.module, "SeedRunJpCombat"))
            : nullptr;
    if (!run)
        throw std::runtime_error(
            "Could not load the replay battle system. Extract the complete build.");
    JpCombatRequest request;
    const auto file_utf8 = file.u8string(), save_utf8 = (root / "replay-session.sav").u8string();
    const std::string playback_file(file_utf8.begin(), file_utf8.end()),
        save(save_utf8.begin(), save_utf8.end());
    request.rom = rom.c_str();
    request.bios = bios.c_str();
    request.save = save.c_str();
    request.replay_path = live ? nullptr : playback_file.c_str();
    request.stage = replay.stage;
    request.surface = {owner.window, owner.renderer, owner.window ? &controls : nullptr, false};
    request.replay_takeover_player = launch.takeover_player;
    request.replay_takeover_frame = launch.takeover_frame;
    request.replay_boxes = launch.boxes;
    LiveReplaySource source;
    if (live) {
        source.context = live;
        source.initial_crc = replay.initial_crc;
        std::copy(replay.initial_timing.begin(), replay.initial_timing.end(),
                  source.initial_timing);
        source.catch_up = !from_start;
        source.join_frame = live->join_frame();
        source.poll = [](void* context, unsigned frame, LiveReplayFrame* out) {
            auto& client = *static_cast<LiveMatchesClient*>(context);
            client.tick();
            if (client.failed()) {
                std::snprintf(out->error, sizeof(out->error), "%s", client.status().c_str());
                return;
            }
            const auto& replay = *client.replay();
            out->available = unsigned(replay.inputs.size());
            if (frame < replay.inputs.size()) {
                out->inputs[0] = replay.inputs[frame][0];
                out->inputs[1] = replay.inputs[frame][1];
            }
            out->ended = client.ended();
            out->complete = replay.complete;
            out->final_crc = replay.final_crc;
        };
        request.live_replay = &source;
    }
    ReplayVideoExportRequest video;
    if (!launch.mp4.empty()) {
        video.output = launch.mp4.c_str();
        video.status = launch.status.c_str();
        video.cancel = launch.cancel.c_str();
        video.height = launch.export_height;
        video.width = video.height == 160 ? 240 : video.height == 720 ? 1280 : 1920;
        video.boxes = launch.boxes;
        request.video_export = &video;
    }
    for (unsigned p = 0; p < 2; ++p) {
        request.fighters[p] = replay.fighters[p];
        request.colors[p] = replay.colors[p];
        request.palettes[p] = replay.palettes[p];
        std::snprintf(request.player_names[p], sizeof(request.player_names[p]), "%s",
                      replay.names[p].c_str());
    }
    ReplayPlaybackResult result;
    request.replay_result = &result;
    const int code = run(&request);
    controls.devices.close();
    std::fprintf(stderr, "replay_playback: frames=%u verified=%u crc=%u %s\n", result.frames,
                 unsigned(result.verified), result.final_crc, result.message);
    if (code != 0 && code != 2)
        throw std::runtime_error(*result.message ? result.message
                                                 : "Replay playback failed. Check the replay log.");
    return !launch.mp4.empty() && !result.verified ? 2 : 0;
}
} // namespace
int watch_match_replay(const std::filesystem::path& file, const std::string& rom,
                       const std::string& bios, int scale, const ReplayLaunchOptions& launch) {
    return run_viewer(read_match_replay(file), file, rom, bios, scale, launch);
}
int watch_live_match(const std::string& id, const std::string& rom, const std::string& bios,
                     int scale, bool from_start, bool boxes) {
    LiveMatchesClient client;
    if (const auto* fixture = std::getenv("SEEDPC_SPECTATOR_FIXTURE"))
        client.fixture(fixture);
    client.watch(id);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!client.failed() && (!client.replay() || client.replay()->inputs.empty())) {
        client.tick();
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Live match timed out. Refresh the match list.");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (client.failed())
        throw std::runtime_error(client.status());
    ReplayLaunchOptions options;
    options.boxes = boxes;
    return run_viewer(*client.replay(), {}, rom, bios, scale, options, &client, from_start);
}
} // namespace seed
