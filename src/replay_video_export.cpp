#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "replay_video_export.h"
#include "mp4_writer.h"
#include "jp_combat_runtime.h"
#include "training_tools.h"
#include "fighter_outline.h"
#include "frame_control.h"
#include "color_lut.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
namespace seed {
namespace {
void status(const ReplayVideoExportRequest& request, const char* phase, unsigned frames,
            unsigned total) {
    if (!request.status || !*request.status)
        return;
    const auto path = std::filesystem::u8path(request.status);
    auto pending = path;
    pending += ".pending";
    std::ofstream f(pending, std::ios::binary | std::ios::trunc);
    f << "{\"phase\":\"" << phase << "\",\"frames\":" << frames << ",\"total\":" << total << "}\n";
    f.close();
    const auto written = !f.fail();
    if (!written || !MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // Progress is advisory. A scanner or reader may briefly deny a rename;
        // retry on the next update without discarding an otherwise valid video.
        const auto code = written ? GetLastError() : ERROR_WRITE_FAULT;
        static bool reported = false;
        if (!reported) {
            std::fprintf(stderr, "replay_export_progress_unavailable: Windows error %lu\n", code);
            reported = true;
        }
        std::error_code error;
        std::filesystem::remove(pending, error);
    }
}
bool cancelled(const ReplayVideoExportRequest& r) {
    return r.cancel && *r.cancel && std::filesystem::exists(std::filesystem::u8path(r.cancel));
}
} // namespace
ReplayPlaybackResult export_replay_video(gbarecomp::NativeFrameControl& machine,
                                         const MatchReplay& replay,
                                         const ReplayVideoExportRequest& request) {
    ReplayPlaybackResult result;
    if (!request.output || !*request.output)
        throw std::runtime_error("Choose an MP4 output path.");
    const auto destination = std::filesystem::absolute(std::filesystem::u8path(request.output));
    if (request.status && *request.status) {
        const auto progress = std::filesystem::absolute(std::filesystem::u8path(request.status));
        if (progress == destination ||
            (progress.extension() != L".json" && progress.extension() != L".log") ||
            std::filesystem::exists(progress))
            throw std::runtime_error("Export progress must use a new .log or .json file.");
    }
    if (destination.extension() != L".mp4" || std::filesystem::exists(destination))
        throw std::runtime_error(
            "MP4 output must be a new .mp4 file. Existing files are preserved.");
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = destination.parent_path() /
                           (destination.stem().wstring() + L"-" +
                            std::filesystem::path(replay_unique_id()).wstring() + L".partial.mp4");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{temporary};
    try {
        status(request, "preparing", 0, unsigned(replay.inputs.size()));
        if (cancelled(request)) {
            status(request, "cancelled", 0, unsigned(replay.inputs.size()));
            std::snprintf(result.message, sizeof(result.message), "Export cancelled.");
            return result;
        }
        {
            Mp4Writer writer(temporary, request.width, request.height);
            std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
            configure_replay_tools(true);
            if (request.boxes) {
                SDL_Event e{};
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.scancode = SDL_SCANCODE_F6;
                training_tools_event(e, false);
            }
            gbarecomp::runtime::ColorSettings colors;
            if (const auto* value = std::getenv("GBARECOMP_SCREEN"))
                gbarecomp::runtime::screen_kind_from_name(value, colors.screen);
            gbarecomp::runtime::ColorLut lut(colors);
            std::array<std::uint8_t, 240 * 160 * 3> graded{};
            for (const auto& input : replay.inputs) {
                if (cancelled(request)) {
                    status(request, "cancelled", result.frames, unsigned(replay.inputs.size()));
                    std::snprintf(result.message, sizeof(result.message), "Export cancelled.");
                    return result;
                }
                set_jp_frame_inputs(input[0], input[1]);
                if (!machine.advance(1023 ^ input[0]))
                    throw std::runtime_error("Replay export stopped before its final frame.");
                machine.drain_audio(audio);
                const auto* pixels =
                    training_display_pixels(outlined_combat_pixels(machine.pixels()), 240, 160);
                if (!lut.is_passthrough()) {
                    lut.map_rgb888(pixels, graded.data(), 240, 160);
                    pixels = graded.data();
                }
                writer.video(pixels);
                for (const auto& block : audio)
                    writer.audio(block.samples, block.rate);
                ++result.frames;
                if (result.frames % 15 == 0)
                    status(request, "rendering", result.frames, unsigned(replay.inputs.size()));
            }
            std::vector<std::uint8_t> state;
            std::string error;
            if (!machine.save(state, error))
                throw std::runtime_error(error);
            result.final_crc = SDL_crc32(0, state.data(), state.size());
            if (replay.complete && result.final_crc != replay.final_crc)
                throw std::runtime_error(
                    "Replay result differs from its recording. MP4 was not saved.");
            result.verified = true;
            if (cancelled(request)) {
                status(request, "cancelled", result.frames, unsigned(replay.inputs.size()));
                result.verified = false;
                std::snprintf(result.message, sizeof(result.message), "Export cancelled.");
                return result;
            }
            status(request, "finalizing", result.frames, unsigned(replay.inputs.size()));
            writer.finish();
        } // Release the sink before renaming its completed file.
        if (cancelled(request)) {
            status(request, "cancelled", result.frames, unsigned(replay.inputs.size()));
            result.verified = false;
            std::snprintf(result.message, sizeof(result.message), "Export cancelled.");
            return result;
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error(
                "Could not save the finished MP4. Existing files are preserved.");
        status(request, "done", result.frames, unsigned(replay.inputs.size()));
        std::snprintf(result.message, sizeof(result.message), "MP4 exported and replay verified.");
        return result;
    } catch (...) {
        try {
            status(request, "failed", result.frames, unsigned(replay.inputs.size()));
        } catch (...) {
        }
        throw;
    }
}
} // namespace seed
