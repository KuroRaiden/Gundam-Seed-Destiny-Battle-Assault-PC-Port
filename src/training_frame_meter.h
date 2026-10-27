#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace seed {
enum class FramePhase : std::uint8_t {
    Idle,
    Startup,
    Active,
    Recovery,
    Hitstun,
    Blockstun,
    Gap,
    Hitstop,
    Unknown
};
struct FighterFrameObservation {
    bool valid = false;
    bool attacking = false;
    bool active = false;
    bool hitstun = false;
    bool blockstun = false;
    bool hitstop = false;
    bool projectile = false;
    std::uint32_t move = 0;
    std::uint16_t animation_id = 0, animation_frame = 0;
};
struct MeterCell {
    FramePhase phase = FramePhase::Idle;
    bool projectile = false;
    std::uint64_t move = 0;
};
struct MoveFrameSummary {
    // Time-to-hit convention: 12 green cells followed by red means Startup 13.
    std::optional<unsigned> startup;
    unsigned preactive = 0, active = 0, recovery = 0, gap = 0;
    unsigned hitstun = 0, hitstop = 0;
    bool interrupted = false;
};
class TrainingFrameMeter {
  public:
    static constexpr std::size_t kHistory = 240;
    void configure(bool training, bool online = false);
    bool toggle();
    void clear();
    void observe(const std::array<FighterFrameObservation, 2>& fighters);
    bool allowed() const {
        return allowed_;
    }
    bool visible() const {
        return allowed_ && visible_;
    }
    std::size_t size() const {
        return count_;
    }
    std::uint64_t frames() const {
        return frames_;
    }
    const MeterCell& cell(unsigned seat, std::size_t oldest_index) const;
    const MoveFrameSummary& summary(unsigned seat) const {
        return tracks_.at(seat).summary;
    }

  private:
    struct Track {
        bool attacking = false, saw_active = false;
        std::uint32_t native_move = 0;
        std::uint16_t animation_id = 0, animation_frame = 0;
        std::uint64_t serial = 0;
        MoveFrameSummary summary;
    };
    void mark_gap(unsigned seat, std::uint64_t serial);
    bool allowed_ = false, visible_ = false;
    std::array<std::array<MeterCell, kHistory>, 2> history_{};
    std::array<Track, 2> tracks_{};
    std::size_t head_ = 0, count_ = 0;
    std::uint64_t frames_ = 0;
    unsigned idle_frames_ = 0;
    bool frozen_ = false;
};
TrainingFrameMeter& training_frame_meter();
} // namespace seed
