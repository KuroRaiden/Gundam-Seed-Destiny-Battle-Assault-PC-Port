#pragma once
#include "game_addresses.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
namespace seed {
struct TrainingBox {
    int x, y, w, h;
    bool attack;
    unsigned actor, index;
    bool connected;
};
struct TrainingView {
    bool ready = false, paused = false;
    unsigned cursor = 0;
    std::array<std::uint8_t, 3> options{};
    std::vector<TrainingBox> boxes;
    template <class Memory> static TrainingView sample(const Memory& memory) {
        namespace jp = address::jp;
        TrainingView out;
        if (memory.u32(jp::kGameState) != jp::kStateTraining)
            return out;
        const auto substate = memory.u32(jp::kSubstate);
        out.ready = substate == 2 || substate == 4;
        out.paused = substate == 4;
        if (!out.ready)
            return out;
        const auto y = memory.u16(jp::kTrainingCursorY);
        constexpr std::array<unsigned, 12> rows{24, 33, 42, 51, 60, 77, 86, 95, 104, 113, 122, 131};
        for (unsigned row = 0; row < rows.size(); ++row)
            if (y == rows[row])
                out.cursor = row;
        for (unsigned i = 0; i < 3; ++i)
            out.options[i] = memory.u8(jp::kTrainingOptions + i);
        // Same integer projection used by the native OBJ composer. Collision
        // offsets are signed bytes; positions are the high halves of 16.16.
        const int camera_x = std::int16_t(memory.u16(jp::kCameraWorldX)) +
                             std::int16_t(memory.u16(jp::kCameraScreenX)) -
                             std::int16_t(memory.u16(jp::kScreenOriginX));
        const int camera_y = std::int16_t(memory.u16(jp::kCameraWorldY)) +
                             std::int16_t(memory.u16(jp::kCameraScreenY)) -
                             std::int16_t(memory.u16(jp::kScreenOriginY));
        out.boxes.reserve(jp::kCollisionActorCount * jp::kCollisionBoxCount);
        for (unsigned n = 0; n < jp::kCollisionActorCount; ++n) {
            const auto actor = jp::kCollisionActors + n * jp::kCollisionActorStride;
            const auto flags = memory.u16(actor);
            if (!(flags & jp::kCollisionEnabledFlag))
                continue;
            const auto position = memory.u32(actor + jp::kCollisionPositionPointerOffset);
            const bool ewram = position >= address::common::kEwramBegin &&
                               position <= address::common::kEwramEnd - 8;
            const bool iwram = position >= address::common::kIwramBegin &&
                               position <= address::common::kIwramEnd - 8;
            if (!ewram && !iwram)
                continue;
            const int x = std::int16_t(memory.u16(position + 2)),
                      y0 = std::int16_t(memory.u16(position + 6));
            const auto count = (memory.u8(actor + jp::kCollisionGeometryCountOffset) >>
                                jp::kCollisionGeometryCountShift) &
                               3;
            for (unsigned i = 0; i < count; ++i) {
                const auto at = actor + jp::kCollisionBoxesOffset + i * jp::kCollisionBoxStride;
                const int dx = std::int8_t(memory.u8(at)), dy = std::int8_t(memory.u8(at + 1));
                const int w = memory.u8(at + 2), h = memory.u8(at + 3);
                const bool attack = (memory.u8(at + 4) & 127) != 0;
                if (!w || !h ||
                    (attack ? (flags & jp::kCollisionDisabledFlag)
                            : (flags & jp::kCollisionInvulnerableFlag)))
                    continue;
                out.boxes.push_back(
                    {x + ((flags & 2) ? -dx - w : dx) - camera_x,
                     y0 + ((flags & 4) ? -dy - h : dy) - camera_y, w, h, attack, n, i,
                     attack && bool(memory.u16(actor + jp::kCollisionHitMaskOffset) & (1u << i))});
            }
        }
        return out;
    }
};
} // namespace seed
