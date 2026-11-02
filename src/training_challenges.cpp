#include "training_challenges.h"
#include "training_tools.h"
#include "training_frame_meter.h"
#include "combat_host.h"
#include "game_addresses.h"
#include "frame_control.h"
#include "runtime_bus_bridge.h"
#include "runtime_arm.h"
#include <atomic>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
namespace seed {
namespace {
namespace jp = address::jp;
enum class Phase { Off, Positioning, Countdown, Running, Result };
enum class GuardTechnique { EscapeBack, EscapeForward, CounterA, CounterB, Other };
bool allowed = false;
Phase phase = Phase::Off;
std::atomic<unsigned> pending{0};
unsigned drill = 0, ticks = 0, attempts = 0;
std::array<unsigned, 4> successes{};
std::array<std::uint16_t, 2> initial_hp{};
std::optional<GuardTechnique> accepted;
bool was_guarding = false;
unsigned previous_thruster = 0;
std::vector<std::uint8_t> original, anchor;
std::string status;
constexpr unsigned capture_anchor = 4;
constexpr int attack_spacing = 40;
constexpr std::array<const char*, 4> names{"Escape backward", "Escape forward", "Guard Counter A",
                                           "Guard Counter B"};
constexpr std::array<const char*, 4> inputs{"Back + R", "Forward + R", "Forward + A",
                                            "Forward + B"};
std::uint32_t player(unsigned seat) {
    return jp::kBattleContext + jp::kFighterRecordsOffset + seat * jp::kFighterStride;
}
int x(unsigned seat) {
    return std::int32_t(bus_read_u32(player(seat) + jp::kFighterWorldXOffset)) / 65536;
}
bool grounded(unsigned seat) {
    return bus_read_u16(player(seat) + jp::kFighterFlagsOffset) & 4;
}
std::uint16_t toward(unsigned seat) {
    return x(seat) < x(1 - seat) ? 16 : 32;
}
void restart() {
    phase = Phase::Countdown;
    ticks = 0;
    ++attempts;
    accepted.reset();
    status = "Get ready. Hold Back to block, then perform the displayed cancel.";
    for (unsigned seat = 0; seat < 2; ++seat)
        initial_hp[seat] = bus_read_u16(player(seat) + jp::kFighterHealthOffset);
}
void restored() {
    release_host_controls();
    training_frame_meter().clear();
    training_presentation_restored();
}
} // namespace
void configure_guard_challenges(bool enable) {
    allowed = enable;
    phase = Phase::Off;
    pending = 0;
    drill = ticks = attempts = 0;
    successes = {};
    original.clear();
    anchor.clear();
    accepted.reset();
    status.clear();
}
void request_guard_challenge(GuardChallengeAction action) {
    if (allowed)
        pending = static_cast<unsigned>(action);
}
bool guard_challenge_active() {
    return allowed && phase != Phase::Off;
}
bool guard_challenge_pending() {
    return allowed && pending.load() != 0;
}
bool service_guard_challenge(gbarecomp::NativeFrameControl& machine) {
    if (!allowed)
        return false;
    const auto action = pending.exchange(0);
    if (!action)
        return false;
    const auto view = training_view();
    if (!view.ready || view.paused) {
        training_snapshot_notice(
            "Resume the Training battle before starting or retrying a challenge.");
        return false;
    }
    std::string error;
    if (action == static_cast<unsigned>(GuardChallengeAction::Stop)) {
        if (!guard_challenge_active())
            return false;
        if (!machine.load(original, error)) {
            training_snapshot_notice("Could not restore the pre-challenge situation: " + error);
            return false;
        }
        phase = Phase::Off;
        original.clear();
        anchor.clear();
        restored();
        training_snapshot_notice(
            "Guard Cancel challenge ended. Original Training situation restored.");
        return true;
    }
    if (action == capture_anchor) {
        if (!guard_challenge_active())
            return false;
        // The scripted dummy bypasses native Training's automatic gauge refill.
        // Fill the drill once; the original situation remains in RAM untouched.
        for (unsigned seat = 0; seat < 2; ++seat) {
            bus_write_u16(player(seat) + jp::kFighterHealthOffset, 10240);
            bus_write_u16(player(seat) + jp::kFighterWeaponGaugeOffset, 7168);
            bus_write_u16(player(seat) + jp::kFighterThrusterOffset, 8192);
        }
        if (!machine.save(anchor, error)) {
            phase = Phase::Result;
            status = "Checkpoint failed. F10 restores the original Training situation.";
            return false;
        }
        restart();
        return false;
    }
    if (!guard_challenge_active()) {
        if (action != static_cast<unsigned>(GuardChallengeAction::Next))
            return false;
        if (!machine.save(original, error)) {
            training_snapshot_notice("Could not start challenge: " + error);
            return false;
        }
        drill = 0;
        attempts = 0;
        successes = {};
        phase = Phase::Positioning;
        ticks = 0;
        anchor.clear();
        status = "Preparing the opponent. F10 returns to your original Training situation.";
        release_host_controls();
        return false;
    }
    if (action == static_cast<unsigned>(GuardChallengeAction::Next)) {
        drill = (drill + 1) % 4;
        attempts = 0;
    }
    if (anchor.empty()) {
        phase = Phase::Positioning;
        ticks = 0;
        return false;
    }
    if (!machine.load(anchor, error)) {
        status = "Retry failed. F10 restores the original situation.";
        return false;
    }
    restored();
    restart();
    return true;
}
void guard_challenge_inputs(std::array<std::uint16_t, 2>& input) {
    if (!guard_challenge_active())
        return;
    input[1] = 0;
    if (phase == Phase::Positioning) {
        input[0] = 0;
        if (grounded(1) && std::abs(x(0) - x(1)) > attack_spacing)
            input[1] = toward(1);
    } else if (phase == Phase::Countdown) {
        input[0] = 0;
    } else if (phase == Phase::Running) {
        // Native close B is a consistent strike for every selectable opponent.
        // Closing distance uses ordinary movement and the player's block stays live.
        if (std::abs(x(0) - x(1)) > attack_spacing)
            input[1] = toward(1);
        else if (ticks % 55 == 0)
            input[1] = 2;
    }
}
void guard_challenge_frame_begin() {
    accepted.reset();
    if (!guard_challenge_active())
        return;
    const auto state = bus_read_u16(player(0));
    was_guarding = state == jp::kFighterStateGuardGround || state == jp::kFighterStateGuardAir;
    previous_thruster = bus_read_u16(player(0) + jp::kFighterThrusterOffset);
}
void guard_challenge_frame_end() {
    if (!guard_challenge_active() || !training_view().ready || training_view().paused)
        return;
    ++ticks;
    if (phase == Phase::Positioning) {
        if (std::abs(x(0) - x(1)) <= attack_spacing && grounded(0) && grounded(1) &&
            bus_read_u16(player(0)) <= 1 && bus_read_u16(player(1)) <= 1)
            pending = capture_anchor;
        else if (ticks >= 600) {
            phase = Phase::Result;
            status = "Could not prepare spacing. F9 retries; F10 returns to Training.";
        }
    } else if (phase == Phase::Countdown && ticks >= 45) {
        phase = Phase::Running;
        ticks = 0;
        status = "Block the opponent, then cancel during blockstun.";
    } else if (phase == Phase::Running) {
        // Native 0x080060C4 gates both validators on guard state and animation
        // progress. Escape/counter acceptance sets 16/17 and spends 3584 PS.
        // A grounded frame can regenerate up to 64 PS after that deduction.
        const auto state = bus_read_u16(player(0));
        const auto ps = bus_read_u16(player(0) + jp::kFighterThrusterOffset);
        if (was_guarding && previous_thruster >= ps + jp::kGuardCancelThrusterCost - 64) {
            const auto motion = bus_read_u8(player(0) + jp::kFighterMotionOffset);
            if (state == jp::kFighterStateGuardEscape) {
                const auto held = bus_read_u16(player(0) + jp::kFighterHeldInputOffset);
                const auto horizontal = held & 48;
                accepted = horizontal == (toward(0) == 16 ? 32 : 16) ? GuardTechnique::EscapeBack
                           : horizontal == toward(0)                 ? GuardTechnique::EscapeForward
                                                                     : GuardTechnique::Other;
            } else if (state == jp::kFighterStateGuardCounter && (motion == 95 || motion == 97))
                accepted = GuardTechnique::CounterA;
            else if (state == jp::kFighterStateGuardCounter && (motion == 94 || motion == 96))
                accepted = GuardTechnique::CounterB;
        }
        if (accepted) {
            phase = Phase::Result;
            if (unsigned(*accepted) == drill) {
                ++successes[drill];
                status = successes[drill] >= 5
                             ? "Challenge complete! F8: next drill. F9: practice again."
                             : "Success! F9: repeat. Goal: five successful cancels.";
            } else
                status = "A different Guard Cancel was accepted. F9: retry this technique.";
        } else if (bus_read_u16(player(0) + jp::kFighterHealthOffset) < initial_hp[0]) {
            phase = Phase::Result;
            status = "You were hit. Hold Back to block first. F9: retry.";
        } else if (ticks >= 360) {
            phase = Phase::Result;
            status = "Time up. Cancel while blocking the opponent. F9: retry.";
        }
    }
}
GuardChallengeView guard_challenge_view() {
    return {guard_challenge_active(),
            drill,
            attempts,
            successes[drill],
            names[drill],
            inputs[drill],
            status};
}
} // namespace seed
