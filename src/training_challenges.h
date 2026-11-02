#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace gbarecomp {
struct NativeFrameControl;
}
namespace seed {
enum class GuardChallengeAction { Next = 1, Retry, Stop };
struct GuardChallengeView {
    bool active = false;
    unsigned drill = 0, attempts = 0, successes = 0;
    std::string_view name, input, status;
};
void configure_guard_challenges(bool allowed);
void request_guard_challenge(GuardChallengeAction);
bool guard_challenge_active();
bool guard_challenge_pending();
bool service_guard_challenge(gbarecomp::NativeFrameControl&);
void guard_challenge_inputs(std::array<std::uint16_t, 2>&);
void guard_challenge_frame_begin();
void guard_challenge_frame_end();
GuardChallengeView guard_challenge_view();
} // namespace seed
