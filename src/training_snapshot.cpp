#include "training_snapshot.h"
#include "training_tools.h"
#include "fighter_outline.h"
#include "gba_ppu.h"
#include "runtime_bus_bridge.h"
#include "portable_paths.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <random>

extern "C" void (*g_seed_obj_line_observer)(unsigned, unsigned, const std::uint8_t*,
                                            const std::uint8_t*, const std::uint8_t*);
namespace seed {
namespace {
class Capture final : public gba::GbaPpu::PresentationObserver {
  public:
    // Presentation state only. Neither this mirror nor the request is saved.
    enum Job { Idle, Requested, Drawing, Ready };
    std::atomic<Job> job{Idle};
    bool allowed = false;
    gba::GbaPpu::PresentationObserver* previous = nullptr;
    gba::GbaPpu mirror;
    FighterOutline outline;
    std::array<std::uint8_t, 240 * 160> objects{};
    unsigned rows = 0;
    bool deliver = false, cached = false;
    inline static Capture* drawing = nullptr;
    static void observe(unsigned y, unsigned width, const std::uint8_t* obj,
                        const std::uint8_t* layers, const std::uint8_t* oam) {
        if (!drawing || width != 240 || y >= 160)
            return;
        drawing->outline.observe(y, width, obj, layers, oam);
        for (unsigned x = 0; x < width; ++x)
            drawing->objects[y * 240 + x] = layers && (layers[x] & 7) == 4;
    }
    void scanline(const gba::GbaPpu& source, std::uint32_t y, std::uint16_t dispcnt,
                  const std::uint8_t* io, const std::uint8_t* vram, const std::uint8_t* oam,
                  const std::uint8_t* pal) override {
        if (previous)
            previous->scanline(source, y, dispcnt, io, vram, oam, pal);
        if (!allowed)
            return;
        if (y == 0 && (job == Idle || job == Requested) && !(dispcnt & 0x80)) {
            const auto view = training_view();
            if (!view.ready || view.paused)
                return;
            // Retain the last combat raster for paused snapshots: the native
            // pause menu reuses OBJ memory for its text and hides the fighters.
            deliver = job == Requested;
            cached = false;
            job = Drawing;
            rows = 0;
            objects.fill(0);
            outline.reset();
        }
        if (job != Drawing)
            return;
        if (y != rows || (dispcnt & 0x80)) {
            job = deliver ? Requested : Idle;
            rows = 0;
            return;
        }
        std::array<std::uint8_t, 0x60> local_io;
        std::copy_n(io, local_io.size(), local_io.begin());
        std::array<std::uint8_t, 0x400> palette;
        std::copy_n(pal, palette.size(), palette.begin());
        // A private palette/backdrop and disabled BG/windows affect only the
        // mirror. GBA device memory and its native framebuffer are untouched.
        const unsigned blue = (16 >> 3) | ((28 >> 3) << 5) | ((43 >> 3) << 10);
        palette[0] = std::uint8_t(blue);
        palette[1] = std::uint8_t(blue >> 8);
        local_io[0x50] = 0;
        local_io[0x51] = 0x20; // Semi-transparent OBJ may blend with the backdrop.
        const auto display = std::uint16_t((dispcnt & ~(0x0f00u | 0xe000u)) | 0x1000u);
        // The ordinary outline tap must observe the real raster, not this
        // second presentation. Gather an independent clean-outline mask.
        const auto old_observer = g_seed_obj_line_observer;
        const auto old_drawing = drawing;
        struct Restore {
            decltype(old_observer) observer;
            Capture* capture;
            ~Restore() {
                g_seed_obj_line_observer = observer;
                drawing = capture;
            }
        } restore{old_observer, old_drawing};
        drawing = this;
        g_seed_obj_line_observer = observe;
        mirror.render_presentation_scanline(source, y, display, local_io.data(), vram, oam,
                                            palette.data());
        ++rows;
    }
    void frame_ready() override {
        if (previous)
            previous->frame_ready();
        if (job == Drawing && rows == 160) {
            mirror.mark_framebuffer_latched();
            cached = true;
            job = deliver ? Ready : Idle;
        }
    }
    void restored() noexcept override {
        if (previous)
            previous->restored();
        job = Idle;
        rows = 0;
        cached = false;
        deliver = false;
        outline.reset();
    }
};
std::unique_ptr<Capture> capture;
} // namespace
void configure_training_snapshots(bool allowed) {
    if (!capture && allowed)
        capture = std::make_unique<Capture>();
    if (!capture)
        return;
    if (auto* ppu = gbarecomp::active_ppu(); ppu && ppu->presentation_observer() == capture.get())
        ppu->set_presentation_observer(capture->previous);
    capture->previous = nullptr;
    capture->allowed = allowed;
    capture->restored();
}
void attach_training_snapshot_observer() {
    if (!capture || !capture->allowed)
        return;
    auto* ppu = gbarecomp::active_ppu();
    if (!ppu || ppu->presentation_observer() == capture.get())
        return;
    capture->previous = ppu->presentation_observer();
    ppu->set_presentation_observer(capture.get());
}
bool request_training_snapshot() {
    if (!capture || !capture->allowed || !training_tools_allowed() || !training_view().ready)
        return false;
    // The ordinary windowed runtime pumps input inside the current raster;
    // NativeFrameControl tests normally request between complete frames.
    // Finish that raster rather than silently discarding F7 during Drawing.
    if (capture->job == Capture::Drawing) {
        if (capture->deliver)
            return false;
        capture->deliver = true;
        training_snapshot_notice("Snapshot queued.");
        return true;
    }
    if (training_view().paused && !capture->cached) {
        training_snapshot_notice("Resume Training once before taking a snapshot.");
        return false;
    }
    attach_training_snapshot_observer();
    auto idle = Capture::Idle;
    const auto requested =
        training_view().paused && capture->cached ? Capture::Ready : Capture::Requested;
    const bool accepted = capture->job.compare_exchange_strong(idle, requested);
    if (accepted)
        training_snapshot_notice("Snapshot queued.");
    return accepted;
}
void reset_training_snapshot() {
    if (capture)
        capture->restored();
}
std::vector<std::uint8_t> take_training_snapshot() {
    if (!capture || !capture->allowed || capture->job != Capture::Ready)
        return {};
    const auto* original = capture->mirror.latched_framebuffer();
    std::vector<std::uint8_t> rgb(original, original + 240 * 160 * 3);
    for (unsigned at = 0; at < 240 * 160; ++at)
        if (!capture->objects[at])
            std::copy(kTrainingSnapshotBlue.begin(), kTrainingSnapshotBlue.end(),
                      rgb.begin() + at * 3);
    if (fighter_outlines_enabled()) {
        capture->outline.set_opacity(fighter_outline_opacity());
        const auto* outlined = capture->outline.apply(rgb.data(), 240, 160);
        if (outlined != rgb.data())
            std::copy_n(outlined, rgb.size(), rgb.begin());
    }
    capture->job = Capture::Idle;
    return rgb;
}
std::filesystem::path training_snapshot_filename(const std::filesystem::path& folder) {
    require_plain_folder(folder);
    static std::atomic<unsigned> serial{0};
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return folder /
           ("Training-" + std::to_string(stamp) + "-" + std::to_string(std::random_device{}()) +
            "-" + std::to_string(serial++) + ".png");
}
} // namespace seed
