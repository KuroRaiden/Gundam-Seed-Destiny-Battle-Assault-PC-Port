#include "jp_frame_probe.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Memory {
    std::vector<std::uint8_t> external = std::vector<std::uint8_t>(262144);
    std::vector<std::uint8_t> internal = std::vector<std::uint8_t>(32768);
    std::uint8_t& byte(std::uint32_t at) {
        if (at >= seed::address::common::kEwramBegin && at < seed::address::common::kEwramEnd)
            return external.at(at - seed::address::common::kEwramBegin);
        return internal.at(at - seed::address::common::kIwramBegin);
    }
    std::uint8_t u8(std::uint32_t at) const {
        return const_cast<Memory*>(this)->byte(at);
    }
    std::uint16_t u16(std::uint32_t at) const {
        return u8(at) | (u8(at + 1) << 8);
    }
    std::uint32_t u32(std::uint32_t at) const {
        return u16(at) | (std::uint32_t(u16(at + 2)) << 16);
    }
    void word(std::uint32_t at, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            byte(at + shift / 8) = std::uint8_t(value >> shift);
    }
};
} // namespace
int main() {
    try {
        namespace jp = seed::address::jp;
        Memory bus;
        seed::JpFrameProbe probe;
        const auto left = jp::kBattleContext + jp::kFighterRecordsOffset;
        const auto right = left + jp::kFighterStride;
        const auto actor = jp::kCollisionActors;
        bus.word(left + jp::kFighterActorPointerOffset, actor);
        bus.word(right + jp::kFighterActorPointerOffset, actor + jp::kCollisionActorStride);
        bus.byte(actor) = 1;
        bus.byte(actor + jp::kCollisionActorStride) = 1;
        bus.byte(left) = jp::kFighterStateAttackGround;
        bus.byte(right) = jp::kFighterStateDamage;
        auto sample = probe.sample(bus, false);
        require(sample[0].valid && sample[0].attacking && !sample[0].active && sample[1].hitstun,
                "Native player state classification failed");
        const auto box = actor + jp::kCollisionBoxesOffset;
        bus.byte(box + 2) = 8;
        bus.byte(box + 3) = 10;
        bus.byte(box + 4) = 0; // Hurtbox, never active attack.
        require(!probe.sample(bus, false)[0].active, "Hurtbox was reported as active");
        bus.byte(box + 4) = 4;
        require(probe.sample(bus, false)[0].active, "Native offensive box missed");
        bus.byte(box + 3) = 0;
        require(!probe.sample(bus, false)[0].active, "Zero-height box was reported active");
        bus.byte(box + 3) = 10;
        bus.byte(actor) = 1 | jp::kCollisionDisabledFlag;
        require(!probe.sample(bus, false)[0].active, "Disabled collider was reported active");
        bus.byte(actor) = 1;
        bus.byte(box + 4) = 0;
        bus.byte(left + jp::kFighterFreezeOffset) = 2;
        require(probe.sample(bus, false)[0].hitstop, "Native animation freeze missed");
        bus.byte(left + jp::kFighterFreezeOffset) = 0;
        require(probe.sample(bus, true)[0].hitstop, "Global slowdown freeze missed");
        const auto projectile = actor + 2 * jp::kCollisionActorStride;
        bus.byte(projectile) = 1;
        bus.byte(projectile + jp::kCollisionTargetsOffset) = (1u << 1)
                                                             << jp::kCollisionTargetsShift;
        const auto projectile_box = projectile + jp::kCollisionBoxesOffset;
        bus.byte(projectile_box + 2) = 5;
        bus.byte(projectile_box + 3) = 5;
        bus.byte(projectile_box + 4) = 3;
        probe.reset();
        sample = probe.sample(bus, false);
        require(sample[0].active && sample[0].projectile && !sample[1].projectile,
                "Projectile ownership/release classification failed");
        sample = probe.sample(bus, false);
        require(!sample[0].active && sample[0].projectile,
                "Lingering projectile recolored owner recovery as active");
        bus.word(left + jp::kFighterActorPointerOffset, seed::address::common::kRomBegin);
        require(!probe.sample(bus, false)[0].valid, "Invalid actor pointer was dereferenced");
        std::puts("PASS: native-layout reader, hurtbox/offense distinction, zero/disabled boxes, "
                  "hitstop, projectile ownership/release, and invalid actor pointer rejection.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
