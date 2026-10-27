#include "fighter_outline.h"
extern "C" void (*g_seed_obj_line_observer)(unsigned, unsigned, const std::uint8_t*,
                                             const std::uint8_t*, const std::uint8_t*);
namespace seed {
namespace {
FighterOutline outline;
bool (*enabled)() = nullptr;
void observe(unsigned y, unsigned width, const std::uint8_t* objects,
             const std::uint8_t* layers, const std::uint8_t* oam) {
    if (enabled && enabled()) outline.observe(y, width, objects, layers, oam);
    else outline.reset();
}
}
void configure_fighter_outline(bool (*active)()) {
    enabled = active;
    outline.reset();
    g_seed_obj_line_observer = active ? observe : nullptr;
}
void reset_fighter_outline() { outline.reset(); }
const std::uint8_t* outlined_combat_pixels(const std::uint8_t* rgb, unsigned w, unsigned h) {
    return enabled && enabled() ? outline.apply(rgb, w, h) : rgb;
}
FighterOutline::Stats fighter_outline_stats() { return outline.stats(); }
} // namespace seed
