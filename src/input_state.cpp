#include "input_state.h"

namespace seed {
void InputState::advance(std::uint16_t buttons) {
    previous = held;
    held = buttons & 0x03FF;
    pressed = held & ~previous;
    released = previous & ~held;
    repeat = pressed;
    for (unsigned bit = 0; bit < repeat_ticks.size(); ++bit) {
        auto& ticks = repeat_ticks[bit];
        ticks = (held & (1u << bit)) ? static_cast<std::uint8_t>(ticks + 1) : 0;
        if (ticks > 13) {
            ticks = 11;
            repeat |= static_cast<std::uint16_t>(1u << bit);
        }
    }
    std::uint16_t recent = 0;
    for (unsigned index = 0; index < 15; ++index) {
        if (index > 8) recent |= history[index];
        history[index] = history[index + 1];
    }
    history[15] = held;
    recent_repress = recent & pressed;
}
} // namespace seed
