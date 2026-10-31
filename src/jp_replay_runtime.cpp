#include "jp_replay_runtime.h"
#include "frame_control.h"
#include "host_window.h"
#include "host_overlay.h"
#include "runtime_bus_bridge.h"
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace seed {
namespace {
JpCombatRequest match;
std::string playback_banner;
struct PlaybackKeys {
    SDL_WindowID window = 0;
    std::atomic<bool> pause{false};
    static bool SDLCALL watch(void* context, SDL_Event* event) {
        auto& keys = *static_cast<PlaybackKeys*>(context);
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
            event->key.windowID == keys.window && event->key.scancode == SDL_SCANCODE_SPACE)
            keys.pause = true;
        return true;
    }
    ~PlaybackKeys() {
        if (window)
            SDL_RemoveEventWatch(watch, this);
    }
};
void check(bool ok, const std::string& error) {
    if (!ok)
        throw std::runtime_error(error);
}
std::filesystem::path recording_folder(const JpCombatRequest& request) {
    return request.replay_directory ? std::filesystem::u8path(request.replay_directory)
                                    : combat_library_path().parent_path() / "replays";
}
MatchReplay metadata(const JpCombatRequest& request, std::uint32_t initial_crc) {
    MatchReplay replay;
    replay.recorded_at = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    replay.online = request.surface.online;
    replay.combat_hash = replay_file_hash(combat_library_path());
    replay.bios_hash = replay_file_hash(std::filesystem::u8path(request.bios));
    replay.initial_crc = initial_crc;
    gbarecomp::RuntimeTimingContext timing;
    gbarecomp::runtime_capture_timing_context(timing);
    replay.initial_timing = {timing.cycles, timing.vblank_starts, timing.yielded_vblank,
                             timing.pending_cycles, std::uint64_t(timing.event_budget)};
    replay.stage = request.stage;
    for (unsigned p = 0; p < 2; ++p) {
        replay.fighters[p] = request.fighters[p];
        replay.colors[p] = request.colors[p];
        replay.palettes[p] = request.palettes[p];
        replay.names[p] = request.player_names[p];
    }
    return replay;
}
void write_recording(const std::filesystem::path& folder, const MatchReplay& replay) noexcept {
    if (replay.inputs.empty())
        return;
    try {
        const auto file = save_match_replay(folder, replay);
        std::fprintf(stderr, "replay_saved: %s (%zu frames, %s)\n",
                     file.filename().string().c_str(), replay.inputs.size(),
                     replay.complete ? "complete" : "partial");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "replay_save_failed: %s\n", error.what());
    }
}
void overlay(gbarecomp::HostOverlay* out) {
    if (playback_banner.empty())
        return;
    out->fill_rect(0, 0, 240, 14, 0, {5, 12, 25, 220}, gbarecomp::OverlaySpace::View);
    out->text(120, 2, 8, {255, 255, 255, 255}, playback_banner.c_str(),
              gbarecomp::OverlayAlign::Center, gbarecomp::OverlaySpace::View);
}
std::uint32_t state_crc(gbarecomp::NativeFrameControl& machine) {
    std::vector<std::uint8_t> state;
    std::string error;
    check(machine.save(state, error), error);
    return SDL_crc32(0, state.data(), state.size());
}
int drive(gbarecomp::NativeFrameControl& machine) {
    ReplayPlaybackResult result;
    MatchReplay replay;
    bool recording = false;
    const bool playback = match.replay_path && *match.replay_path;
    const auto directory = recording_folder(match);
    try {
        if (playback) {
            replay = read_match_replay(std::filesystem::u8path(match.replay_path));
            check(replay.combat_hash == replay_file_hash(combat_library_path()),
                  "This replay needs its original game build: " + replay.build_version);
            check(replay.bios_hash == replay_file_hash(std::filesystem::u8path(match.bios)),
                  "This replay needs the same BIOS it was recorded with.");
            for (unsigned p = 0; p < 2; ++p)
                check(replay.fighters[p] == match.fighters[p] &&
                          replay.colors[p] == match.colors[p] &&
                          replay.palettes[p] == match.palettes[p],
                      "Replay match configuration differs.");
            check(replay.stage == match.stage, "Replay stage differs.");
        }
        gbarecomp::HostWindow window;
        if (match.surface.window) {
            check(window.open(3, 240, 160,
                              playback ? "Gundam SEED - Replay"
                                       : "Gundam SEED Destiny / Battle Assault - Local VS"),
                  "Cannot open the battle window.");
            window.load_input_config(SDL_GetBasePath(), false);
            window.set_host_overlay(overlay);
            window.set_network_session(false);
        }
        PlaybackKeys keys;
        if (playback && window.is_open()) {
            keys.window = SDL_GetWindowID(match.surface.window);
            check(SDL_AddEventWatch(PlaybackKeys::watch, &keys),
                  "Cannot initialize replay controls.");
        }
        bool quit = false, paused = false, fast = false, overlay_pause = false;
        auto pump = [&] {
            if (!window.is_open())
                return;
            const auto events = window.pump();
            quit |= events.quit;
            if (events.toggle_fullscreen)
                window.set_fullscreen(window.fullscreen() ? 0 : 1);
            if (events.window_bigger)
                window.adjust_scale(1);
            if (events.window_smaller)
                window.adjust_scale(-1);
            if (events.volume_up)
                window.set_volume(window.volume() + 5);
            if (events.volume_down)
                window.set_volume(window.volume() - 5);
            if (events.toggle_fps)
                window.set_fps_readout(!window.fps_readout());
            if (events.toggle_pause)
                paused = !paused;
            if (keys.pause.exchange(false) && !events.remap_pause) {
                paused = !paused;
                std::fprintf(stderr, "replay_pause: %s at frame %u\n",
                             paused ? "paused" : "resumed", result.frames);
            }
            fast = events.fast_forward;
            overlay_pause = events.remap_pause;
        };
        machine.seed_clock(946684800);
        std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
        for (unsigned boot = 0; boot < 600 && !jp_combat_startup_ready(); ++boot) {
            set_jp_frame_inputs(0, 0);
            check(machine.advance(1023), "Replay battle startup stopped.");
            machine.drain_audio(audio);
            if (boot % 16 == 0)
                pump();
            if (quit)
                return 0;
        }
        check(jp_combat_startup_ready(), "Replay battle initialization did not finish.");
        if (playback) {
            // Runtime counters survive a return to character selection. Save
            // these small scheduling values so a rematch also replays in a
            // fresh process, without embedding CPU or cartridge memory.
            const auto& t = replay.initial_timing;
            gbarecomp::runtime_restore_timing_values({t[0], t[1], t[2], t[3], std::int64_t(t[4])});
        }
        const auto initial = state_crc(machine);
        if (playback)
            check(initial == replay.initial_crc,
                  "Replay starting state differs; playback stopped.");
        else {
            try {
                replay = metadata(match, initial);
                recording = true;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "replay_recording_unavailable: %s\n", error.what());
            }
        }
        auto deadline = std::chrono::steady_clock::now();
        while (!quit) {
            pump();
            if (quit)
                break;
            if (playback) {
                playback_banner = std::string(paused || overlay_pause ? "PAUSED" : "REPLAY") +
                                  "  " + std::to_string(result.frames / 60) + " / " +
                                  std::to_string(replay.inputs.size() / 60) +
                                  "s | Space: pause  Esc: exit";
            }
            if (paused || overlay_pause) {
                if (window.is_open())
                    window.present(machine.pixels());
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                deadline = std::chrono::steady_clock::now();
                continue;
            }
            if (playback && result.frames >= replay.inputs.size()) {
                result.final_crc = state_crc(machine);
                check(!replay.complete || result.final_crc == replay.final_crc,
                      "Replay result differs from its recording; playback stopped.");
                result.verified = true;
                break;
            }
            std::array<std::uint16_t, 2> input{};
            if (playback)
                input = replay.inputs[result.frames];
            else if (match.sample_inputs)
                match.sample_inputs(input.data());
            input[0] &= 1023;
            input[1] &= 1023;
            set_jp_frame_inputs(input[0], input[1]);
            check(machine.advance(1023 ^ input[0]), "Battle frame stopped.");
            machine.drain_audio(audio);
            if (recording && replay.inputs.size() < kMaxReplayFrames)
                replay.inputs.push_back(input);
            ++result.frames;
            if (window.is_open()) {
                if (!fast)
                    for (const auto& block : audio)
                        window.push_audio_block(block.samples.data(), block.samples.size(),
                                                block.rate);
                window.present(machine.pixels());
            }
            if (!playback && (jp_combat_ended() || (match.qualification_frames &&
                                                    result.frames >= match.qualification_frames))) {
                result.final_crc = state_crc(machine);
                if (recording && replay.inputs.size() == result.frames) {
                    replay.complete = true;
                    replay.final_crc = result.final_crc;
                }
                break;
            }
            if (window.is_open() && !fast) {
                deadline += std::chrono::microseconds(16743);
                if (deadline < std::chrono::steady_clock::now() - std::chrono::milliseconds(50))
                    deadline = std::chrono::steady_clock::now();
                std::this_thread::sleep_until(deadline);
            } else
                deadline = std::chrono::steady_clock::now();
        }
        if (recording)
            write_recording(directory, replay);
        std::snprintf(result.message, sizeof(result.message), "%s",
                      result.verified ? "Replay finished and verified." : "Playback closed.");
        if (match.replay_result)
            *match.replay_result = result;
        playback_banner.clear();
        return 0;
    } catch (const std::exception& error) {
        if (recording)
            write_recording(directory, replay);
        std::snprintf(result.message, sizeof(result.message), "%s", error.what());
        if (match.replay_result)
            *match.replay_result = result;
        std::fprintf(stderr, "replay_failed: %s\n", error.what());
        playback_banner.clear();
        return 1;
    }
}
} // namespace
OnlineReplayRecording::OnlineReplayRecording(GekkoSession* session, const JpCombatRequest& request,
                                             std::uint32_t crc)
    : replay_(metadata(request, crc)), directory_(recording_folder(request)) {
    check(gekko_start_recording(session, false, true), "Cannot record confirmed match inputs.");
    session_ = session;
}
OnlineReplayRecording::~OnlineReplayRecording() {
    finish(0, 0, false);
}
void OnlineReplayRecording::finish(unsigned expected_frames, std::uint32_t final_crc,
                                   bool complete) noexcept {
    if (!session_)
        return;
    auto* session = session_;
    session_ = nullptr;
    try {
        unsigned size = 0;
        const auto* bytes = gekko_stop_recording(session, &size);
        if (!bytes || !size)
            return;
        // Decode only the trusted blob produced by this recording session.
        // Files supplied by users use our bounded input-only parser instead.
        GekkoSession* decoder = nullptr;
        check(gekko_create(&decoder, GekkoReplaySession), "Cannot finish the replay recording.");
        struct Owner {
            GekkoSession*& s;
            ~Owner() {
                gekko_destroy(&s);
            }
        } owner{decoder};
        check(gekko_load_replay(decoder, bytes, size), "Cannot decode confirmed replay inputs.");
        bool finished = false;
        while (!finished && replay_.inputs.size() < kMaxReplayFrames) {
            int count = 0;
            auto** events = gekko_update_session(decoder, &count);
            for (int i = 0; i < count; ++i) {
                check(events[i]->type == GekkoAdvanceEvent,
                      "Replay recording unexpectedly contains machine state.");
                const auto* input =
                    reinterpret_cast<const std::uint16_t*>(events[i]->data.adv.inputs);
                replay_.inputs.push_back({input[0], input[1]});
            }
            auto** status = gekko_session_events(decoder, &count);
            for (int i = 0; i < count; ++i)
                finished |= status[i]->type == GekkoReplayFinished;
        }
        replay_.complete = complete && finished && replay_.inputs.size() == expected_frames;
        replay_.final_crc = replay_.complete ? final_crc : 0;
        write_recording(directory_, replay_);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "replay_save_failed: %s\n", error.what());
    }
}
std::unique_ptr<OnlineReplayRecording> begin_online_recording(GekkoSession* session,
                                                              const JpCombatRequest& request,
                                                              std::uint32_t crc) noexcept {
    try {
        return std::make_unique<OnlineReplayRecording>(session, request, crc);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "replay_recording_unavailable: %s\n", error.what());
        return {};
    }
}
void configure_jp_replays(gbarecomp::RunOptions& options, const JpCombatRequest& request) {
    match = request;
    playback_banner.clear();
    options.native_frame_driver = drive;
}
} // namespace seed
