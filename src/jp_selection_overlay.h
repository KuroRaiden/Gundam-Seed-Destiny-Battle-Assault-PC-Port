#pragma once
#include <cstdint>
#include <vector>
namespace gbarecomp {
class HostOverlay;
}
namespace seed {
void draw_jp_selection(gbarecomp::HostOverlay* out, const std::vector<std::uint16_t>& pixels);
}
