#include "jp_live_runtime.h"
#include "live_matches.h"
#include "frame_control.h"
#include "host_window.h"
#include "training_tools.h"
#include "runtime_bus_bridge.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace seed {
namespace {
JpCombatRequest match;
void require(bool ok, const std::string& error) {
    if (!ok)
        throw std::runtime_error(error);
}
std::uint32_t checksum(gbarecomp::NativeFrameControl& machine) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    require(machine.save(bytes, error), error);
    return SDL_crc32(0, bytes.data(), bytes.size());
}
int drive(gbarecomp::NativeFrameControl& machine) {
    ReplayPlaybackResult result;
    try {
        const auto& source = *match.live_replay;
        gbarecomp::HostWindow window;
        if (match.surface.window) {
            require(window.open(3, 240, 160, "Gundam SEED - Spectator"),
                    "Cannot open spectator window.");
            window.load_input_config(SDL_GetBasePath(), false);
            window.set_network_session(false);
        }
        bool quit = false, paused = false, overlay_pause = false;
        auto pump = [&] {
            if (!window.is_open())
                return;
            const auto event = window.pump();
            quit = event.quit;
            if (event.toggle_pause)
                paused = !paused;
            overlay_pause = event.remap_pause;
            if (event.toggle_fullscreen)
                window.set_fullscreen(window.fullscreen() ? 0 : 1);
            if (event.window_bigger)
                window.adjust_scale(1);
            if (event.window_smaller)
                window.adjust_scale(-1);
            if (event.volume_up)
                window.set_volume(window.volume() + 5);
            if (event.volume_down)
                window.set_volume(window.volume() - 5);
        };
        gbarecomp::runtime_restore_timing_context({});
        machine.seed_clock(946684800);
        std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
        for (unsigned boot = 0; boot < 600 && !jp_combat_startup_ready(); ++boot) {
            set_jp_frame_inputs(0, 0);
            require(machine.advance(1023), "Spectator battle initialization stopped.");
            machine.drain_audio(audio);
            if (boot % 16 == 0)
                pump();
            if (quit)
                return 0;
        }
        require(jp_combat_startup_ready(), "Spectator battle initialization did not finish.");
        const auto* timing = source.initial_timing;
        gbarecomp::runtime_restore_timing_values(
            {timing[0], timing[1], timing[2], timing[3], std::int64_t(timing[4])});
        require(checksum(machine) == source.initial_crc,
                "Live match needs the same game build and BIOS.");
        if (match.replay_boxes) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.scancode = SDL_SCANCODE_F6;
            training_tools_event(event, false);
        }
        auto deadline = std::chrono::steady_clock::now();
        std::string last_title;
        bool catching_up = source.catch_up, buffering = true;
        while (!quit) {
            pump();
            LiveReplayFrame frame;
            source.poll(source.context, result.frames, &frame);
            require(!*frame.error, frame.error);
            require(result.frames <= frame.available, "Live input stream moved backwards.");
            const auto behind = frame.available - result.frames;
            if (catching_up && frame.available >= source.join_frame && behind <= kSpectatorDelayFrames) {
                std::fprintf(stderr, "spectator_catchup_complete: frame=%u buffered=%u\n",
                             result.frames, behind);
                catching_up = false;
                deadline = std::chrono::steady_clock::now();
                window.clear_queued_audio();
            }
            if (frame.ended || behind >= kSpectatorDelayFrames)
                buffering = false;
            if (!frame.ended && !behind)
                buffering = true;
            const std::string title = std::string(match.player_names[0]) + " vs " +
                                      match.player_names[1] + " | Spectating | " +
                                      (catching_up               ? "Catching up | "
                                       : paused || overlay_pause ? "Paused | "
                                       : buffering               ? "Buffering | "
                                                                 : "") +
                                      std::to_string(result.frames / 60) + "s | " +
                                      std::to_string(behind / 60) +
                                      "s behind | F6: boxes | Pause: pause | Esc: exit";
            if (title != last_title && window.is_open()) {
                window.set_title(title.c_str());
                last_title = title;
            }
            if (quit)
                break;
            if (paused || overlay_pause || buffering) {
                if (window.is_open())
                    window.present(machine.pixels());
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                deadline = std::chrono::steady_clock::now();
                continue;
            }
            if (frame.ended && !behind) {
                result.final_crc = checksum(machine);
                require(!frame.complete || result.final_crc == frame.final_crc,
                        "Spectator result differs from the match recording.");
                result.verified = frame.complete;
                break;
            }
            set_jp_frame_inputs(frame.inputs[0], frame.inputs[1]);
            require(machine.advance(1023 ^ frame.inputs[0]), "Spectator playback stopped.");
            machine.drain_audio(audio);
            ++result.frames;
            if (window.is_open() && (!catching_up || result.frames % 8 == 0)) {
                if (!catching_up)
                    for (const auto& block : audio)
                        window.push_audio_block(block.samples.data(), block.samples.size(),
                                                block.rate);
                window.present(machine.pixels());
            }
            if (!catching_up) {
                deadline += std::chrono::microseconds(16743);
                if (deadline < std::chrono::steady_clock::now() - std::chrono::milliseconds(50))
                    deadline = std::chrono::steady_clock::now();
                std::this_thread::sleep_until(deadline);
            }
        }
        std::snprintf(result.message, sizeof(result.message), "%s",
                      result.verified ? "Live match finished and verified."
                                      : "Spectator session closed.");
        if (match.replay_result)
            *match.replay_result = result;
        return 0;
    } catch (const std::exception& error) {
        std::snprintf(result.message, sizeof(result.message), "%s", error.what());
        if (match.replay_result)
            *match.replay_result = result;
        std::fprintf(stderr, "spectator_failed: %s\n", error.what());
        return 1;
    }
}
} // namespace
void configure_jp_live(gbarecomp::RunOptions& options, const JpCombatRequest& request) {
    match = request;
    configure_replay_tools(true);
    options.native_frame_driver = drive;
}
} // namespace seed
