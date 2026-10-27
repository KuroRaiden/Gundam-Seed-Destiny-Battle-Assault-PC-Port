#pragma once
#include <cstdint>
namespace seed {
// The native Destiny rotation enumerates these 25 stages. Special mode-only
// entry 25 is outside normal versus/training selection.
inline constexpr unsigned kJpStageCount = 25;
inline constexpr std::uint8_t kRandomStage = 255;
constexpr bool valid_jp_stage(unsigned stage) { return stage < kJpStageCount; }
constexpr bool valid_stage_cursor(unsigned stage) {
    return valid_jp_stage(stage) || stage == kRandomStage;
}
} // namespace seed
