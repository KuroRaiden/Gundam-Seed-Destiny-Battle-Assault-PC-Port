#include "jp_selection_overlay.h"
#include "host_overlay.h"
#include "color_lut.h"
#include <cstdlib>
namespace seed {
void draw_jp_selection(gbarecomp::HostOverlay* out, const std::vector<std::uint16_t>& pixels) {
    using namespace gbarecomp;
    static const runtime::ColorLut colors([] {
        runtime::ColorSettings settings;
        if (const char* screen = std::getenv("GBARECOMP_SCREEN"))
            runtime::screen_kind_from_name(screen, settings.screen);
        return settings;
    }());
    // Coalesce horizontal runs for the native renderer without changing the
    // runtime overlay ABI. Pixel coordinates scale with the game viewport.
    for (unsigned y = 0; y < 160; ++y)
        for (unsigned x = 0; x < 240;) {
            const auto p = pixels[y * 240 + x];
            unsigned end = x + 1;
            while (end < 240 && pixels[y * 240 + end] == p)
                ++end;
            OverlayColor c{static_cast<std::uint8_t>((p & 31) * 255 / 31),
                           static_cast<std::uint8_t>(((p >> 5) & 31) * 255 / 31),
                           static_cast<std::uint8_t>(((p >> 10) & 31) * 255 / 31), 255};
            if (!colors.is_passthrough()) {
                const std::uint8_t input[]{c.r, c.g, c.b};
                std::uint8_t output[3];
                colors.map_rgb888(input, output, 1, 1);
                c.r = output[0];
                c.g = output[1];
                c.b = output[2];
            }
            out->fill_rect(x, y, end - x, 1, 0, c, OverlaySpace::View);
            x = end;
        }
}
} // namespace seed
