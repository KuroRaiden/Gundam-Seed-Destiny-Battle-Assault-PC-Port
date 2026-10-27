#include "fighter_outline.h"
extern "C" void (*g_seed_obj_line_observer)(unsigned, unsigned, const std::uint8_t*,
                                            const std::uint8_t*, const std::uint8_t*);
namespace seed {
namespace {
FighterOutline outline;
bool (*combat_active)() = nullptr;
bool show_outlines = true;
void observe(unsigned y, unsigned width, const std::uint8_t* objects, const std::uint8_t* layers,
             const std::uint8_t* oam) {
    if (show_outlines && combat_active && combat_active())
        outline.observe(y, width, objects, layers, oam);
    else
        outline.reset();
}
} // namespace
void configure_fighter_outline(bool (*active)()) {
    combat_active = active;
    outline.reset();
    g_seed_obj_line_observer = active ? observe : nullptr;
}
void set_fighter_outlines_enabled(bool enabled) {
    if (show_outlines != enabled)
        outline.reset();
    show_outlines = enabled;
}
bool fighter_outlines_enabled() {
    return show_outlines;
}
void set_fighter_outline_opacity(unsigned percent) {
    outline.set_opacity(percent);
}
unsigned fighter_outline_opacity() {
    return outline.opacity();
}
void reset_fighter_outline() {
    outline.reset();
}
const std::uint8_t* outlined_combat_pixels(const std::uint8_t* rgb, unsigned w, unsigned h) {
    return show_outlines && combat_active && combat_active() ? outline.apply(rgb, w, h) : rgb;
}
FighterOutline::Stats fighter_outline_stats() {
    return outline.stats();
}
} // namespace seed
