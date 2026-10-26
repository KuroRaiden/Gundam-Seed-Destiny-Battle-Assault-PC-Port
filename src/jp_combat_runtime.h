#pragma once
#include "jp_combat.h"
#include "runtime.h"
namespace seed {
void configure_jp_combat(gbarecomp::RunOptions& options, const JpCombatRequest& request);
void set_jp_frame_inputs(std::uint16_t p1, std::uint16_t p2);
bool jp_combat_ended();
} // namespace seed
