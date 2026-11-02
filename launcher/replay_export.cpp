#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "replay_export.h"
#include "match_replay.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
namespace seed::launcher {
ReplayExportJob::~ReplayExportJob() {
    cancel();
}
bool ReplayExportJob::start(const std::filesystem::path& root, const std::filesystem::path& replay,
                            const std::filesystem::path& jp, const std::filesystem::path& bios,
                            unsigned height, bool boxes, std::string& error) {
    if (running()) {
        error = "An MP4 export is already running.";
        return false;
    }
    if (height != 160 && height != 720 && height != 1080) {
        error = "Choose native, 720p or 1080p output.";
        return false;
    }
    try {
        std::filesystem::create_directories(root / "Logs");
        std::filesystem::create_directories(root / "replays/exports");
        const auto id = replay_unique_id();
        status_ = root / "Logs" / ("replay-export-" + id + ".log");
        cancel_ = root / "Logs" / ("replay-export-" + id + ".cancel");
        log_ = root / "Logs/last-replay-export.log";
        state_ = {};
        cancel_requested_ = false;
        state_.output =
            root / "replays/exports" /
            (replay.stem().wstring() + L"-" + std::to_wstring(height) + L"p-" + wide(id) + L".mp4");
        std::vector<std::wstring> args{L"--watch-replay",  replay.wstring(),
                                       L"--jp-rom",        jp.wstring(),
                                       L"--bios",          bios.wstring(),
                                       L"--export-mp4",    state_.output.wstring(),
                                       L"--export-height", std::to_wstring(height),
                                       L"--export-status", status_.wstring(),
                                       L"--export-cancel", cancel_.wstring()};
        if (boxes)
            args.push_back(L"--replay-boxes");
        if (!process_.start(root / "GundamSeedRecomp.exe", args, log_, error))
            return false;
        state_.phase = ExportPhase::Running;
        state_.message = "Preparing replay export...";
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
void ReplayExportJob::cancel() {
    if (!running() || cancel_requested_)
        return;
    std::ofstream f(cancel_, std::ios::binary | std::ios::trunc);
    f << "cancel\n";
    f.close();
    if (f.fail()) {
        state_.message = "Could not request cancellation. The export is still running.";
        return;
    }
    cancel_requested_ = true;
    state_.message = "Cancelling export...";
}
void ReplayExportJob::tick() {
    if (!running())
        return;
    char text[512]{};
    // Let the worker atomically replace progress while the launcher reads it.
    const auto file = CreateFileW(status_.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD bytes = 0;
        ReadFile(file, text, sizeof(text) - 1, &bytes, nullptr);
        CloseHandle(file);
    }
    unsigned frames = 0, total = 0;
    char phase[24]{};
    if (std::sscanf(text, "{\"phase\":\"%23[^\"]\",\"frames\":%u,\"total\":%u}", phase, &frames,
                    &total) == 3 &&
        total && frames <= total) {
        state_.progress = float(frames) / total;
        if (!cancel_requested_)
            state_.message =
                std::string(phase) == "finalizing"
                    ? "Finalizing MP4..."
                    : "Exporting frame " + std::to_string(frames) + " / " + std::to_string(total);
    }
    if (const auto code = process_.poll()) {
        std::error_code file_error;
        if (*code == 0 && std::filesystem::is_regular_file(state_.output, file_error)) {
            state_.phase = ExportPhase::Done;
            state_.progress = 1;
            state_.message = "MP4 export complete.";
        } else if (*code == 2) {
            state_.phase = ExportPhase::Cancelled;
            state_.message = "Export cancelled.";
        } else {
            state_.phase = ExportPhase::Failed;
            state_.message = "MP4 export failed. Open the export log for details.";
        }
        for (auto p : {status_, std::filesystem::path(status_.wstring() + L".pending"), cancel_}) {
            std::error_code error;
            std::filesystem::remove(p, error);
        }
    }
}
} // namespace seed::launcher
