#include "local_vs.h"

namespace seed {
std::uint8_t LocalVsSelection::advance(const MatchInputs& inputs) {
    if (cancelled || launch)
        return 0;
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
    }
    if (cancelled) {
        ready_frames = 0;
        return changed;
    }
    ready_frames = seats[0].ready && seats[1].ready ? ready_frames + 1 : 0;
    launch = ready_frames >= 18;
    return changed;
}
} // namespace seed
