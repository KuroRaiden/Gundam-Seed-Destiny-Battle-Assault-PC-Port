#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "replay_export.h"
#include "match_replay.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 5, "Pass Release folder, replay, JP ROM and BIOS");
        const auto root = std::filesystem::absolute(argv[1]),
                   replay = std::filesystem::absolute(argv[2]);
        const auto hash = seed::replay_file_hash(replay);
        std::string error;
        seed::launcher::ReplayExportJob job;
        bool progress = false;
        auto finish = [&](bool cancel, bool lock_progress = false) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            bool requested = false;
            while (job.running() && std::chrono::steady_clock::now() < deadline) {
                job.tick();
                if (job.state().progress > 0 && job.state().progress < 1) {
                    progress = true;
                    if (lock_progress) {
                        std::vector<HANDLE> readers;
                        for (const auto& file :
                             std::filesystem::directory_iterator(root / "Logs")) {
                            if (!file.path().filename().wstring().starts_with(L"replay-export-") ||
                                file.path().extension() != L".log")
                                continue;
                            const auto reader = CreateFileW(file.path().c_str(), GENERIC_READ,
                                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                            nullptr, OPEN_EXISTING, 0, nullptr);
                            if (reader != INVALID_HANDLE_VALUE)
                                readers.push_back(reader);
                        }
                        require(!readers.empty(), "No progress file available for contention test");
                        std::this_thread::sleep_for(std::chrono::milliseconds(700));
                        for (auto reader : readers)
                            CloseHandle(reader);
                        lock_progress = false;
                    }
                    if (cancel && !requested) {
                        job.cancel();
                        requested = true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(!job.running(), "Export job did not finish or cancel promptly");
            if (cancel)
                require(requested, "No cancellable export progress was received");
        };
        require(job.start(root, replay, argv[3], argv[4], 160, false, error), error.c_str());
        finish(false, true);
        require(job.state().phase == seed::launcher::ExportPhase::Done, "Launcher MP4 job failed");
        require(progress && std::filesystem::file_size(job.state().output) > 4096,
                "No live progress or MP4 data");
        std::ifstream log(root / "Logs/last-replay-export.log");
        const std::string text{std::istreambuf_iterator<char>(log),
                               std::istreambuf_iterator<char>()};
        log.close();
        require(text.find("replay_export_progress_unavailable") != std::string::npos,
                "Progress-lock contention was not exercised");
        std::cout
            << "PASS: launcher export progress, MP4 completion and progress-file contention\n";
        require(job.start(root, replay, argv[3], argv[4], 1080, true, error), error.c_str());
        finish(true);
        require(job.state().phase == seed::launcher::ExportPhase::Cancelled,
                "Cancel did not complete cleanly");
        require(!std::filesystem::exists(job.state().output),
                "Cancel left a misleading completed MP4");
        for (const auto& file :
             std::filesystem::directory_iterator(job.state().output.parent_path()))
            require(
                !file.path().filename().wstring().starts_with(job.state().output.stem().wstring()),
                "Cancel left a partial MP4");
        require(seed::replay_file_hash(replay) == hash, "MP4 export modified its recording");
        std::cout
            << "PASS: launcher cancellation, partial-file cleanup and recording preservation\n";
        require(job.start(root, replay, argv[3], root / "missing-test-bios.bin", 160, false, error),
                error.c_str());
        finish(false);
        require(job.state().phase == seed::launcher::ExportPhase::Failed,
                "Export errors must report failure without waiting for a hidden dialog");
        require(!std::filesystem::exists(job.state().output), "Failed export left a video");
        std::cout
            << "PASS: export startup failure returns to the launcher without a modal dialog\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
