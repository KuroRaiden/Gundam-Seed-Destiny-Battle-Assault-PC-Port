#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
namespace seed {
// The same rational clock as the GBA. No rounded 60 Hz audio/video drift.
inline constexpr std::uint64_t kReplayClock = 16777216, kReplayFrameCycles = 280896;
inline constexpr std::uint64_t replay_video_time(std::uint64_t frame) {
    return frame * kReplayFrameCycles * 10000000 / kReplayClock;
}
class Mp4Writer {
  public:
    Mp4Writer(const std::filesystem::path&, unsigned width, unsigned height);
    ~Mp4Writer();
    void video(const std::uint8_t* rgb240x160);
    void audio(std::span<const std::int16_t> stereo, unsigned rate);
    void finish();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace seed
