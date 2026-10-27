#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace seed {
// Host presentation data only: never serialized, sent or written into guest RAM.
class FighterOutline {
  public:
    static constexpr unsigned width = 240, height = 160, pixels = width * height;
    struct Stats {
        unsigned fighter_pixels[2]{}, black_pixels = 0;
    };
    void reset() {
        ready_ = false;
        next_row_ = 0;
        stats_ = {};
    }
    void observe(unsigned y, unsigned w, const std::uint8_t* objects, const std::uint8_t* layers,
                 const std::uint8_t* oam) {
        if (w != width || y >= height) {
            reset();
            return;
        }
        if (!y)
            next_row_ = 0;
        if (y != next_row_) {
            // Discard an interrupted raster without invalidating the frame
            // that the window may still be displaying.
            next_row_ = height;
            return;
        }
        for (unsigned x = 0; x < width; ++x) {
            const auto at = y * width + x;
            pending_owners_[at] = 0;
            pending_layers_[at] = layers ? layers[x] : 255; // Blank/invalid rows stay protected.
            if (!objects || !oam || objects[x] >= 128)
                continue;
            const auto* sprite = oam + objects[x] * 8;
            const unsigned a0 = sprite[0] | (unsigned(sprite[1]) << 8);
            const unsigned a2 = sprite[4] | (unsigned(sprite[5]) << 8);
            const unsigned tile = a2 & 1023, palette = a2 >> 12;
            // The JP body buffers occupy 256 tiles/3 palette banks per seat.
            // This is ownership metadata, independent of RGB/custom colors.
            if ((a0 & 8192) || ((a0 >> 10) & 3) > 1)
                continue;
            if (tile < 256 && palette < 3)
                pending_owners_[at] = 1;
            else if (tile >= 256 && tile < 512 && palette >= 3 && palette < 6)
                pending_owners_[at] = 2;
        }
        if (++next_row_ == height) {
            // Publish only complete metadata. The PPU latches its RGB frame
            // after the last visible row; partial next frames must not clear
            // or overwrite the corresponding presentation mask.
            owners_.swap(pending_owners_);
            layers_.swap(pending_layers_);
            ready_ = true;
        }
    }
    const std::uint8_t* apply(const std::uint8_t* rgb, unsigned w, unsigned h) {
        stats_ = {};
        if (!rgb || !ready_ || w != width || h != height)
            return rgb;
        std::copy_n(rgb, rgb_.size(), rgb_.begin());
        for (unsigned at = 0; at < pixels; ++at)
            if (owners_[at])
                ++stats_.fighter_pixels[owners_[at] - 1];
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto at = y * width + x;
                const unsigned layer = layers_[at] & 7, priority = layers_[at] >> 3;
                // Do not paint a fighter, another object, HUD BG2/BG3, or blanking.
                if (owners_[at] || (layer != 0 && layer != 1 && layer != 5))
                    continue;
                bool edge = false;
                for (int dy = -1; dy <= 1 && !edge; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = int(x) + dx, ny = int(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height))
                            continue;
                        const auto neighbor = unsigned(ny) * width + unsigned(nx);
                        if (owners_[neighbor] && priority >= (layers_[neighbor] >> 3)) {
                            edge = true;
                            break;
                        }
                    }
                if (edge) {
                    if (rgb_[at * 3] || rgb_[at * 3 + 1] || rgb_[at * 3 + 2])
                        ++stats_.black_pixels;
                    rgb_[at * 3] = rgb_[at * 3 + 1] = rgb_[at * 3 + 2] = 0;
                }
            }
        return rgb_.data();
    }
    const Stats& stats() const {
        return stats_;
    }

  private:
    std::array<std::uint8_t, pixels> owners_{}, layers_{};
    std::array<std::uint8_t, pixels> pending_owners_{}, pending_layers_{};
    std::array<std::uint8_t, pixels * 3> rgb_{};
    unsigned next_row_ = 0;
    bool ready_ = false;
    Stats stats_;
};
void configure_fighter_outline(bool (*active)());
void set_fighter_outlines_enabled(bool enabled);
bool fighter_outlines_enabled();
void reset_fighter_outline();
const std::uint8_t* outlined_combat_pixels(const std::uint8_t*, unsigned = 240, unsigned = 160);
FighterOutline::Stats fighter_outline_stats();
} // namespace seed
