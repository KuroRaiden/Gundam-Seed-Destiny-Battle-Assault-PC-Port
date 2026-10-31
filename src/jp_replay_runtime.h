#pragma once
#include "jp_combat_runtime.h"
#include "match_replay.h"
#include "gekkonet.h"
#include <memory>
namespace seed {
class OnlineReplayRecording {
  public:
    OnlineReplayRecording(GekkoSession* session, const JpCombatRequest& request, std::uint32_t crc);
    ~OnlineReplayRecording();
    void finish(unsigned expected_frames, std::uint32_t final_crc, bool complete) noexcept;

  private:
    GekkoSession* session_ = nullptr;
    MatchReplay replay_;
    std::filesystem::path directory_;
};
std::unique_ptr<OnlineReplayRecording> begin_online_recording(GekkoSession* session,
                                                              const JpCombatRequest& request,
                                                              std::uint32_t crc) noexcept;
void configure_jp_replays(gbarecomp::RunOptions& options, const JpCombatRequest& request);
} // namespace seed
