#include "training_view.h"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
struct Memory {
    std::array<std::uint8_t, 262144> ewram{};
    std::array<std::uint8_t, 32768> iwram{};
    auto& at(std::uint32_t address) {
        return address >= seed::address::common::kIwramBegin
                   ? iwram.at(address - seed::address::common::kIwramBegin)
                   : ewram.at(address - seed::address::common::kEwramBegin);
    }
    std::uint8_t u8(std::uint32_t address) const {
        return const_cast<Memory*>(this)->at(address);
    }
    unsigned u16(std::uint32_t a) const {
        return u8(a) | (unsigned(u8(a + 1)) << 8);
    }
    unsigned u32(std::uint32_t a) const {
        return u16(a) | (u16(a + 2) << 16);
    }
    void put(std::uint32_t a, unsigned value, unsigned bytes = 4) {
        for (unsigned i = 0; i < bytes; ++i)
            at(a + i) = std::uint8_t(std::uint64_t(value) >> (i * 8));
    }
};
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        namespace jp = seed::address::jp;
        Memory m;
        m.put(jp::kGameState, jp::kStateTraining);
        m.put(jp::kSubstate, 2);
        m.put(jp::kCameraWorldX, 40, 2);
        m.put(jp::kCameraWorldY, 30, 2);
        m.put(jp::kCameraScreenX, 5, 2);
        m.put(jp::kCameraScreenY, 6, 2);
        m.put(jp::kScreenOriginX, 2, 2);
        m.put(jp::kScreenOriginY, 3, 2);
        const auto actor = jp::kCollisionActors,
                   position = jp::kBattleContext + jp::kFighterRecordsOffset + 36;
        m.put(actor, 1, 2);
        m.put(actor + jp::kCollisionPositionPointerOffset, position);
        m.put(position, 100u << 16);
        m.put(position + 4, 120u << 16);
        m.put(actor + jp::kCollisionGeometryCountOffset, 2 << jp::kCollisionGeometryCountShift, 1);
        const auto box = actor + jp::kCollisionBoxesOffset;
        m.put(box, 0, 8);
        m.put(box, std::uint8_t(-12), 1);
        m.put(box + 1, std::uint8_t(-40), 1);
        m.put(box + 2, 20, 1);
        m.put(box + 3, 30, 1);
        m.put(box + jp::kCollisionBoxStride, std::uint8_t(-4), 1);
        m.put(box + jp::kCollisionBoxStride + 1, std::uint8_t(-20), 1);
        m.put(box + jp::kCollisionBoxStride + 2, 16, 1);
        m.put(box + jp::kCollisionBoxStride + 3, 12, 1);
        m.put(box + jp::kCollisionBoxStride + 4, 1, 1);
        auto view = seed::TrainingView::sample(m);
        require(view.boxes.size() == 2, "Native count mismatch");
        require(view.boxes[0].x == 45 && view.boxes[0].y == 47 && !view.boxes[0].attack,
                "Signed offset/camera projection failed");
        require(view.boxes[1].x == 53 && view.boxes[1].y == 67 && view.boxes[1].attack,
                "Offensive geometry failed");
        m.put(actor, 7, 2);
        view = seed::TrainingView::sample(m);
        require(view.boxes[0].x == 49 && view.boxes[0].y == 97, "Mirrored geometry failed");
        m.put(actor, 1 | jp::kCollisionInvulnerableFlag, 2);
        view = seed::TrainingView::sample(m);
        require(view.boxes.size() == 1 && view.boxes[0].attack, "Invulnerability showed a hurtbox");
        m.put(actor, 1 | jp::kCollisionDisabledFlag, 2);
        view = seed::TrainingView::sample(m);
        require(view.boxes.size() == 1 && !view.boxes[0].attack, "Disabled attack still displayed");
        m.put(actor, 0, 2);
        require(seed::TrainingView::sample(m).boxes.empty(), "Inactive actor displayed");
        m.put(actor, 1, 2);
        m.put(actor + jp::kCollisionPositionPointerOffset, 0);
        require(seed::TrainingView::sample(m).boxes.empty(), "Invalid actor pointer accepted");
        m.put(jp::kGameState, jp::kStateVersus);
        require(!seed::TrainingView::sample(m).ready, "VS allowed Training view");
        std::cout << "PASS: native geometry counts, signed offsets, both flips, camera, "
                     "invulnerability, disabled attacks and scene guards.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
