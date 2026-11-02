#pragma once
#include "platform.h"
namespace seed::launcher {
enum class ExportPhase { Idle, Running, Done, Cancelled, Failed };
struct ReplayExportState {
    ExportPhase phase = ExportPhase::Idle;
    float progress = 0;
    std::string message;
    std::filesystem::path output;
};
class ReplayExportJob {
  public:
    ~ReplayExportJob();
    bool start(const std::filesystem::path& root, const std::filesystem::path& replay,
               const std::filesystem::path& jp, const std::filesystem::path& bios, unsigned height,
               bool boxes, std::string& error);
    void tick();
    void cancel();
    bool running() const {
        return process_.running();
    }
    const ReplayExportState& state() const {
        return state_;
    }

  private:
    GameProcess process_;
    ReplayExportState state_;
    std::filesystem::path status_, cancel_, log_;
    bool cancel_requested_ = false;
};
} // namespace seed::launcher
