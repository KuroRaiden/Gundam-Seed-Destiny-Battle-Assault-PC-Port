#include "fighter_outline.h"
#include <cstdio>
#include <stdexcept>
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        seed::FighterOutline outline;
        std::array<std::uint8_t, 1024> oam{};
        oam[4] = 1; // P1 body tile/palette.
        oam[12] = 0;
        oam[13] = 49; // P2 tile 256, palette 3.
        oam[20] = 128;
        oam[21] = 98; // Unrelated effect/shadow, bank 6.
        std::array<std::uint8_t, 240 * 160 * 3> raw;
        raw.fill(100);
        const auto before = raw;
        auto submit = [&](bool complete) {
            for (unsigned y = 0; y < (complete ? 160u : 80u); ++y) {
                std::array<std::uint8_t, 240> objects, layers;
                objects.fill(255);
                layers.fill(24); // Stage BG0, priority 3.
                if (y == 10) {
                    objects[10] = 0;
                    layers[10] = 12;
                    objects[9] = 2;
                    layers[9] = 12; // Effect must be preserved.
                    layers[11] = 2; // HUD must be preserved.
                }
                if (y == 9)
                    layers[10] = 0; // Foreground in front of fighter.
                if (y == 20) {
                    objects[20] = 1;
                    layers[20] = 12;
                }
                if (y == 30) {
                    objects[30] = 2;
                    layers[30] = 12;
                }
                outline.observe(y, 240, objects.data(), layers.data(), oam.data());
            }
        };
        submit(false);
        require(outline.apply(raw.data(), 240, 160) == raw.data(), "Partial frame was outlined");
        submit(true);
        const auto* display = outline.apply(raw.data(), 240, 160);
        require(raw == before, "Outline modified original pixels");
        require(outline.stats().fighter_pixels[0] == 1 && outline.stats().fighter_pixels[1] == 1,
                "Fighter ownership was not resolved");
        require(outline.stats().black_pixels == 13,
                "Outline thickness or protected pixels changed");
        for (auto at : {10 * 240 + 10, 20 * 240 + 20, 10 * 240 + 9, 10 * 240 + 11, 9 * 240 + 10,
                        30 * 240 + 30, 30 * 240 + 31})
            require(display[at * 3] == 100, "Fighter/HUD/effect/foreground was overwritten");
        require(display[(11 * 240 + 10) * 3] == 0, "One-pixel black contour is missing");
        const auto edge = (11 * 240 + 10) * 3;
        for (const unsigned opacity : {0u, 1u, 25u, 50u, 75u, 99u, 100u}) {
            outline.set_opacity(opacity);
            const auto* blended = outline.apply(raw.data(), 240, 160);
            require(blended[edge] == (100 - opacity), "Outline opacity blending is incorrect");
            require(raw == before, "Opacity changed source pixels");
            require(blended[(10 * 240 + 10) * 3] == 100 && blended[(10 * 240 + 11) * 3] == 100,
                    "Opacity changed fighter or HUD pixels");
            if (!opacity)
                require(blended == raw.data() && outline.stats().black_pixels == 0,
                        "Zero opacity did not bypass presentation");
        }
        raw[edge] = 255;
        raw[edge + 1] = 127;
        raw[edge + 2] = 1;
        outline.set_opacity(50);
        const auto* rounded = outline.apply(raw.data(), 240, 160);
        require(rounded[edge] == 128 && rounded[edge + 1] == 64 && rounded[edge + 2] == 1,
                "Opacity rounding lost low RGB values");
        raw.fill(100);
        outline.set_opacity(999);
        require(outline.opacity() == 100, "Opacity maximum was not clamped");
        // A window can re-present the last latched framebuffer while the next
        // frame is only partly rasterized. Keep its complete outline stable.
        std::array<std::uint8_t, 240> next_objects, next_layers;
        next_objects.fill(255);
        next_layers.fill(24);
        for (unsigned row = 0; row < 80; ++row) {
            outline.observe(row, 240, next_objects.data(), next_layers.data(), oam.data());
            require(outline.apply(raw.data(), 240, 160) != raw.data(),
                    "Outline disappeared while the next raster frame was incomplete");
            require(outline.stats().black_pixels == 13,
                    "Partial next-frame ownership replaced the displayed frame");
        }
        outline.observe(100, 240, next_objects.data(), next_layers.data(), oam.data());
        outline.apply(raw.data(), 240, 160);
        require(outline.stats().black_pixels == 13, "Interrupted raster discarded displayed mask");
        for (unsigned row = 0; row < 160; ++row)
            outline.observe(row, 240, next_objects.data(), next_layers.data(), oam.data());
        outline.apply(raw.data(), 240, 160);
        require(outline.stats().black_pixels == 0, "Complete next frame was not published");
        outline.reset();
        require(outline.apply(raw.data(), 240, 160) == raw.data(),
                "Restoration retained a stale mask");
        require(outline.apply(raw.data(), 480, 320) == raw.data(), "Wrong-sized image was changed");
        std::puts("PASS: black contour, raw-frame immutability, HUD/effects/foreground, partial "
                  "frames, opacity blending and restore invalidation");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
