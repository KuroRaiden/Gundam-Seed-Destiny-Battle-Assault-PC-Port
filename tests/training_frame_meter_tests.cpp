#include "training_frame_meter.h"
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        seed::TrainingFrameMeter meter;
        std::array<seed::FighterFrameObservation, 2> sample{};
        sample[0].valid = sample[1].valid = true;
        meter.configure(false);
        require(!meter.toggle(), "VS enabled the training meter");
        meter.observe(sample);
        require(meter.size() == 0, "VS recorded training history");
        meter.configure(true, true);
        require(!meter.toggle(), "Online enabled the training meter");
        meter.configure(true);
        require(!meter.visible() && meter.toggle() && meter.visible(), "Training toggle failed");
        sample[0].attacking = true;
        sample[0].move = 77;
        for (unsigned frame = 0; frame < 12; ++frame) {
            sample[0].animation_frame = static_cast<std::uint16_t>(frame);
            meter.observe(sample);
        }
        require(!meter.summary(0).startup, "Startup was invented before an active frame");
        sample[0].active = true;
        for (unsigned frame = 12; frame < 15; ++frame) {
            sample[0].animation_frame = static_cast<std::uint16_t>(frame);
            meter.observe(sample);
        }
        require(meter.summary(0).startup == 13u && meter.summary(0).active == 3,
                "Time-to-hit startup convention is off by one");
        sample[0].active = false;
        for (unsigned frame = 15; frame < 18; ++frame) {
            sample[0].animation_frame = static_cast<std::uint16_t>(frame);
            meter.observe(sample);
        }
        sample[0].hitstop = true;
        for (int frame = 0; frame < 3; ++frame)
            meter.observe(sample);
        sample[0].hitstop = false;
        sample[0].active = true;
        sample[0].animation_frame = 18;
        meter.observe(sample);
        require(meter.summary(0).gap == 3 && meter.summary(0).recovery == 0 &&
                    meter.summary(0).hitstop == 3 && meter.summary(0).active == 4,
                "Multi-hit gap or hitstop duration was miscounted");
        for (unsigned frame = 15; frame < 18; ++frame)
            require(meter.cell(0, frame).phase == seed::FramePhase::Gap,
                    "Multi-hit gap was left blue");
        sample[0].active = false;
        sample[0].animation_frame = 19;
        sample[1].hitstun = true;
        meter.observe(sample);
        require(meter.cell(1, meter.size() - 1).phase == seed::FramePhase::Hitstun,
                "Opponent hitstun missing");
        sample[0].animation_id = 1;
        sample[0].animation_frame = 0;
        meter.observe(sample);
        require(meter.summary(0).startup == 13u && meter.summary(0).active == 4,
                "An animation continuation was mistaken for a new move");
        sample[0].move = 78;
        sample[0].animation_frame = 0;
        meter.observe(sample);
        require(!meter.summary(0).startup && meter.summary(0).preactive == 1,
                "Cancel did not start a new startup sequence");
        sample[0].hitstun = true;
        meter.observe(sample);
        require(meter.summary(0).interrupted, "Interrupted attack was reported complete");
        sample = {};
        sample[0].valid = sample[1].valid = true;
        for (int frame = 0; frame < 40; ++frame)
            meter.observe(sample);
        const auto frozen_frames = meter.frames();
        meter.observe(sample);
        require(meter.frames() == frozen_frames, "Last sequence did not freeze after idle");
        sample[0].attacking = true;
        sample[0].move = 100;
        meter.observe(sample);
        require(meter.size() == 1, "New action did not reset the frozen timeline");
        for (unsigned frame = 1; frame < 500; ++frame) {
            sample[0].animation_frame = static_cast<std::uint16_t>(frame);
            meter.observe(sample);
        }
        require(meter.size() == meter.kHistory && meter.frames() == 500,
                "Bounded history wrapped incorrectly");
        require(meter.toggle() && !meter.visible(), "F5 close equivalent failed");
        meter.configure(false);
        require(!meter.visible() && meter.size() == 0, "Leaving Training leaked the meter");
        std::puts("PASS: training/online gate, toggle, 13f startup, multi-hit gaps, "
                  "hitstop, "
                  "hitstun, cancels, interruptions, idle freeze and 240-frame wrap.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
