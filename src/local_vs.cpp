#include "local_vs.h"

namespace seed {
void LocalVsSelection::back_to_fighters() {
    stage_active = stage_confirmed = launch = false;
    stage_release_guard = true;
    ready_frames = 0;
    for (auto& seat : seats)
        seat.ready = false;
}
std::uint8_t LocalVsSelection::advance(const MatchInputs& inputs) {
    if (cancelled || launch)
        return 0;
    if (stage_enabled && stage_active) {
        for (unsigned seat = 0; seat < 2; ++seat)
            if ((!training || seat == 0) && (local_seat_mask & (1u << seat)) &&
                (inputs.players[seat].pressed & 2)) {
                if (local_seat_mask & 1)
                    back_to_fighters();
                else
                    seats[seat].ready = false; // Host publishes the shared return to fighters.
                return 1;
            }
        if (!(local_seat_mask & 1))
            return 0;
        const auto& input = inputs.players[0];
        if (stage_release_guard) {
            if (!input.held)
                stage_release_guard = false;
            return 0;
        }
        const int direction = (input.repeat & (32 | 64)) ? -1
                              : (input.repeat & (16 | 128)) ? 1 : 0;
        if (direction) {
            const int before = stage_cursor == kRandomStage ? kJpStageCount : stage_cursor;
            const auto next = (before + int(kJpStageCount) + 1 + direction) % (kJpStageCount + 1);
            stage_cursor = next == kJpStageCount ? kRandomStage : std::uint8_t(next);
        }
        if (input.pressed & 1) {
            if (stage_cursor == kRandomStage) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                stage = std::uint8_t(random % kJpStageCount);
            } else
                stage = stage_cursor;
            stage_confirmed = launch = true;
        }
        return direction || stage_confirmed ? 1 : 0;
    }
    std::uint8_t changed = 0;
    const unsigned training_seat = seats[0].ready ? 1 : 0;
    if (training && training_seat == 1 && !seats[1].ready && (inputs.players[0].pressed & 2)) {
        seats[0].ready = false;
        ready_frames = 0;
        return 1;
    }
    for (unsigned player = 0; player < 2; ++player) {
        if (training && player != training_seat)
            continue;
        auto& seat = seats[player];
        const auto& input = inputs.players[training ? 0 : player];
        if (input.pressed & 2) {
            if (seat.ready)
                seat.ready = false;
            else
                cancelled = true;
            continue;
        }
        if (seat.ready)
            continue;
        const auto before = seat.cursor;
        const int direction = (input.repeat & 0x30) == 0x20   ? -1
                              : (input.repeat & 0x30) == 0x10 ? 1
                                                              : 0;
        if (jp_layout) {
            const int vertical = (input.repeat & 0xC0) == 0x40   ? -1
                                 : (input.repeat & 0xC0) == 0x80 ? 1
                                                                 : 0;
            if (direction || vertical) {
                unsigned row = seat.cursor / 8, column = seat.cursor % 8;
                for (unsigned attempt = 0; attempt < 8; ++attempt) {
                    if (direction)
                        column = (column + 8 + direction) % 8;
                    else
                        row = (row + 4 + vertical) % 4;
                    const auto next = row * 8 + column;
                    if (kJpPortraits[next] != 255 || next == 11 || next == 12) {
                        seat.cursor = static_cast<std::uint8_t>(next);
                        break;
                    }
                }
            }
        } else if (direction && available_slots) {
            do
                seat.cursor = (seat.cursor + 13 + direction) % 13;
            while (!(available_slots & (1u << seat.cursor)));
        }
        if (before != seat.cursor) {
            seat.fighter = jp_layout ? kJpPortraits[seat.cursor] : kVsPortraits[seat.cursor];
            seat.color = 0;
            changed |= 1u << player;
        }
        if (jp_layout && (input.pressed & 8) && supported(seat.fighter)) {
            seat.color = std::uint8_t((seat.color + 1) % jp_color_count(seat.fighter));
            changed |= 1u << player;
        }
        if (input.pressed & 1) {
            if (seat.fighter == 255) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                const unsigned count = jp_layout ? 32 : 13;
                unsigned slot = random % count;
                for (unsigned attempt = 0; attempt < count; ++attempt, slot = (slot + 1) % count) {
                    const auto fighter = jp_layout ? kJpPortraits[slot] : kVsPortraits[slot];
                    if (supported(fighter) && (jp_layout || (available_slots & (1u << slot)))) {
                        seat.fighter = fighter;
                        break;
                    }
                }
                if (seat.fighter == 255)
                    continue;
                changed |= 1u << player;
            }
            if (supported(seat.fighter))
                seat.ready = true;
        }
        if (jp_layout && palette_for && (local_seat_mask & (1u << player)))
            seat.palette = palette_for(player, seat.fighter, seat.color);
    }
    if (cancelled) {
        ready_frames = 0;
        return changed;
    }
    ready_frames = seats[0].ready && seats[1].ready ? ready_frames + 1 : 0;
    if (stage_enabled) {
        if (ready_frames >= 18 && (local_seat_mask & 1)) {
            stage_active = stage_release_guard = true;
            stage_confirmed = false;
            ready_frames = 0;
            return 1;
        }
    } else
        launch = ready_frames >= 18;
    return changed;
}
} // namespace seed
