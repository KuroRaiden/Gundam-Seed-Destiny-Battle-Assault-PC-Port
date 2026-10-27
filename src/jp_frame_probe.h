#pragma once
#include "game_addresses.h"
#include "training_frame_meter.h"

namespace seed {
// Read-only adapter. The callback is installed at the native battle update's
// tail; it is never driven by the SDL present rate or by wall-clock time.
class JpFrameProbe {
  public:
    void reset() {
        previous_projectiles_ = {};
    }
    template <class Bus>
    std::array<FighterFrameObservation, 2> sample(const Bus& bus, bool global_hitstop) {
        namespace jp = address::jp;
        std::array<FighterFrameObservation, 2> result{};
        for (unsigned seat = 0; seat < 2; ++seat) {
            auto& out = result[seat];
            const auto player =
                jp::kBattleContext + jp::kFighterRecordsOffset + seat * jp::kFighterStride;
            const auto actor = bus.u32(player + jp::kFighterActorPointerOffset);
            if (!ram(actor, jp::kCollisionActorStride))
                continue;
            out.valid = true;
            const auto state = bus.u16(player);
            out.hitstun = state == jp::kFighterStateDamage;
            out.blockstun =
                state == jp::kFighterStateGuardGround || state == jp::kFighterStateGuardAir;
            out.hitstop =
                global_hitstop || bus.u8(player + jp::kFighterFreezeOffset) != 0 ||
                (bus.u16(player + jp::kFighterFlagsOffset) & jp::kFighterAnimationFrozenFlag);
            out.active = offense(bus, actor);
            const auto animation = player + jp::kFighterAnimationOffset;
            out.animation_id = bus.u16(animation + jp::kAnimationIdOffset);
            // One accepted command can use several animation clips. Keep its
            // startup/active/recovery together; a same-clip restart still marks
            // a new action in the timeline, as do command-index changes.
            const auto command =
                state == jp::kFighterStateAttackGround || state == jp::kFighterStateAttackAir
                    ? bus.u8(player + jp::kFighterCommandVariantOffset)
                : state == jp::kFighterStateThrow ? 0
                                                  : out.animation_id;
            out.move = (std::uint32_t(state) << 16) | command;
            out.animation_frame = bus.u16(animation + jp::kAnimationTickOffset);
            out.attacking =
                !out.hitstun && !out.blockstun &&
                (state == jp::kFighterStateAttackGround || state == jp::kFighterStateAttackAir ||
                 state == jp::kFighterStateThrow || animation_attacks(bus, animation));
            std::uint64_t projectiles = 0;
            bool projectile_active = false;
            for (unsigned index = 0; index < jp::kCollisionActorCount; ++index) {
                const auto candidate = jp::kCollisionActors + index * jp::kCollisionActorStride;
                if (candidate == actor || !(bus.u16(candidate) & jp::kCollisionEnabledFlag))
                    continue;
                const auto targets =
                    bus.u16(candidate + jp::kCollisionTargetsOffset) >> jp::kCollisionTargetsShift;
                // Native target masks distinguish the other seat from our own.
                if (!(targets & (1u << (1 - seat))) || (targets & (1u << seat)))
                    continue;
                projectiles |= std::uint64_t(1) << index;
                projectile_active |= offense(bus, candidate);
            }
            // The projectile's release belongs to the attack. Its later flight
            // is shown separately and does not turn the owner's recovery red.
            if (out.attacking && (projectiles & ~previous_projectiles_[seat]))
                out.active = true;
            out.projectile = projectile_active;
            previous_projectiles_[seat] = projectiles;
        }
        return result;
    }

  private:
    static bool ram(std::uint32_t pointer, std::uint32_t bytes) {
        return pointer >= address::common::kEwramBegin &&
               pointer <= address::common::kEwramEnd - bytes;
    }
    template <class Bus> static bool offense(const Bus& bus, std::uint32_t actor) {
        namespace jp = address::jp;
        const auto flags = bus.u16(actor);
        if (!(flags & jp::kCollisionEnabledFlag) || (flags & jp::kCollisionDisabledFlag))
            return false;
        for (unsigned index = 0; index < jp::kCollisionBoxCount; ++index) {
            const auto box = actor + jp::kCollisionBoxesOffset + index * jp::kCollisionBoxStride;
            if (bus.u8(box + 2) && bus.u8(box + 3) && (bus.u8(box + 4) & 127))
                return true;
        }
        return false;
    }
    template <class Bus> static bool animation_attacks(const Bus& bus, std::uint32_t animation) {
        namespace jp = address::jp;
        const auto bank = bus.u32(animation);
        const auto id = bus.u16(animation + jp::kAnimationIdOffset);
        if (bank >= jp::kAnimationDescriptorCount || id >= jp::kAnimationDescriptorCount - bank)
            return false;
        const auto descriptor =
            address::common::kEwramBegin + (bank + id) * jp::kAnimationDescriptorStride;
        const auto count = bus.u8(descriptor + 2);
        const auto records = bus.u32(descriptor + 12);
        if (!count || !ram(records, count * jp::kAnimationHitboxRecordStride))
            return false;
        for (unsigned record = 0; record < count; ++record)
            for (unsigned box = 0; box < jp::kCollisionBoxCount; ++box) {
                if (record && !(bus.u16(records + record * jp::kAnimationHitboxRecordStride + 2) &
                                (1u << box)))
                    continue;
                const auto geometry = records + record * jp::kAnimationHitboxRecordStride + 4 +
                                      box * jp::kCollisionBoxStride;
                if (bus.u8(geometry + 2) && bus.u8(geometry + 3) && (bus.u8(geometry + 4) & 127))
                    return true;
            }
        return false;
    }
    std::array<std::uint64_t, 2> previous_projectiles_{};
};
} // namespace seed
