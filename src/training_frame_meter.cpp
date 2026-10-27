#include "training_frame_meter.h"
#include <stdexcept>

namespace seed {
void TrainingFrameMeter::configure(bool training, bool online) {
    allowed_ = training && !online;
    visible_ = false;
    clear();
}
bool TrainingFrameMeter::toggle() {
    if (!allowed_)
        return false;
    visible_ = !visible_;
    return true;
}
void TrainingFrameMeter::clear() {
    history_ = {};
    tracks_ = {};
    head_ = count_ = 0;
    frames_ = 0;
    idle_frames_ = 0;
    frozen_ = false;
}
const MeterCell& TrainingFrameMeter::cell(unsigned seat, std::size_t index) const {
    if (seat > 1 || index >= count_)
        throw std::out_of_range("Frame meter history index");
    return history_[seat][(head_ + kHistory - count_ + index) % kHistory];
}
void TrainingFrameMeter::mark_gap(unsigned seat, std::uint64_t serial) {
    // A later active segment proves that the preceding blue cells were a gap
    // between hits. Never repaint hitstop or another move's recovery.
    for (std::size_t back = 1; back <= count_; ++back) {
        auto& previous = history_[seat][(head_ + kHistory - back) % kHistory];
        if (previous.move != serial)
            break;
        if (previous.phase == FramePhase::Hitstop)
            continue;
        if (previous.phase != FramePhase::Recovery)
            break;
        previous.phase = FramePhase::Gap;
    }
    auto& summary = tracks_[seat].summary;
    // Recovery can exceed the visible history for exceptionally long moves.
    summary.gap += summary.recovery;
    summary.recovery = 0;
}
void TrainingFrameMeter::observe(const std::array<FighterFrameObservation, 2>& fighters) {
    if (!allowed_)
        return;
    bool activity = false;
    for (const auto& fighter : fighters)
        activity |= fighter.valid && (fighter.attacking || fighter.active || fighter.hitstun ||
                                      fighter.blockstun || fighter.projectile || fighter.hitstop);
    if (frozen_) {
        if (!activity)
            return;
        clear();
    }
    // Ignore all-invalid scene/setup observations rather than drawing fake idle.
    if (!fighters[0].valid && !fighters[1].valid)
        return;
    for (unsigned seat = 0; seat < 2; ++seat) {
        const auto& observation = fighters[seat];
        auto& track = tracks_[seat];
        MeterCell next;
        next.projectile = observation.projectile;
        if (!observation.valid) {
            next.phase = FramePhase::Unknown;
        } else if (observation.hitstop) {
            next.phase = FramePhase::Hitstop;
            next.move = track.serial;
            if (track.attacking)
                ++track.summary.hitstop;
        } else if (observation.hitstun || observation.blockstun) {
            if (track.attacking)
                track.summary.interrupted = true;
            track.attacking = false;
            next.phase = observation.hitstun ? FramePhase::Hitstun : FramePhase::Blockstun;
            if (observation.hitstun)
                ++track.summary.hitstun;
        } else if (observation.attacking) {
            const bool new_move = !track.attacking || observation.move != track.native_move ||
                                  (observation.animation_id == track.animation_id &&
                                   observation.animation_frame < track.animation_frame);
            if (new_move) {
                ++track.serial;
                track.summary = {};
                track.saw_active = false;
            }
            track.attacking = true;
            track.native_move = observation.move;
            track.animation_id = observation.animation_id;
            track.animation_frame = observation.animation_frame;
            next.move = track.serial;
            if (observation.active) {
                if (!track.saw_active)
                    track.summary.startup = track.summary.preactive + 1;
                else if (track.summary.recovery)
                    mark_gap(seat, track.serial);
                track.saw_active = true;
                ++track.summary.active;
                next.phase = FramePhase::Active;
            } else if (!track.saw_active) {
                ++track.summary.preactive;
                next.phase = FramePhase::Startup;
            } else {
                ++track.summary.recovery;
                next.phase = FramePhase::Recovery;
            }
        } else {
            track.attacking = false;
            next.phase = observation.active ? FramePhase::Active : FramePhase::Idle;
        }
        history_[seat][head_] = next;
    }
    head_ = (head_ + 1) % kHistory;
    if (count_ < kHistory)
        ++count_;
    ++frames_;
    idle_frames_ = activity ? 0 : idle_frames_ + 1;
    frozen_ = idle_frames_ >= 12;
}
TrainingFrameMeter& training_frame_meter() {
    static TrainingFrameMeter meter;
    return meter;
}
} // namespace seed
