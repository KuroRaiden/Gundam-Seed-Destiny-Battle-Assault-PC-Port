#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "training_tools.h"
#include "us_training_art.h"
#include "frame_control.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "live_controls.h"
#include "combat_host.h"
#include <array>
#include <atomic>
#include <fstream>
#include <cstdio>
#include <stdexcept>
namespace seed {
namespace {
bool allowed = false, boxes_visible = false;
std::atomic<unsigned> pending{0};
std::atomic<bool> busy{false};
std::filesystem::path state_path;
UsTrainingArt art;
std::string message;
std::uint64_t message_until = 0;
std::array<std::uint8_t, 240 * 160 * 3> display{};
// These are raw, const RAM views. bus_read_u* would alter native open-bus
// bookkeeping and must never be used by an optional presentation feature.
struct Memory {
    const gba::GbaBus* bus;
    std::uint8_t u8(std::uint32_t address) const {
        namespace common = seed::address::common;
        if (!bus)
            return 0;
        if (address >= common::kEwramBegin && address < common::kEwramEnd)
            return bus->ewram_ptr()[address - common::kEwramBegin];
        if (address >= common::kIwramBegin && address < common::kIwramEnd)
            return bus->iwram_ptr()[address - common::kIwramBegin];
        return 0;
    }
    std::uint16_t u16(std::uint32_t at) const {
        return u8(at) | (unsigned(u8(at + 1)) << 8);
    }
    std::uint32_t u32(std::uint32_t at) const {
        return u16(at) | (unsigned(u16(at + 2)) << 16);
    }
};
void notice(std::string text) {
    std::fprintf(stderr, "training: %s\n", text.c_str());
    message = std::move(text);
    message_until = SDL_GetTicks() + 4500;
}
void pixel(int x, int y, std::array<std::uint8_t, 3> color, unsigned alpha = 255) {
    if (x < 0 || x >= 240 || y < 0 || y >= 160)
        return;
    for (unsigned c = 0; c < 3; ++c) {
        auto& value = display[(y * 240 + x) * 3 + c];
        value = std::uint8_t((unsigned(value) * (255 - alpha) + unsigned(color[c]) * alpha + 127) /
                             255);
    }
}
void word(unsigned frame, int x, int y, bool heading = false, unsigned first_row = 0,
          unsigned rows = 160) {
    const auto& image = art.image(frame);
    for (unsigned row = first_row; row < std::min(image.h, first_row + rows); ++row)
        for (unsigned col = 0; col < image.w; ++col) {
            const auto value = image.pixels[row * image.w + col];
            if (!value)
                continue;
            if (value >= 7 && value <= 10) {
                const auto shade = std::uint8_t(175 + (value - 7) * 26);
                pixel(x + col, y + row - first_row,
                      heading ? std::array<std::uint8_t, 3>{255, shade, 255}
                              : std::array<std::uint8_t, 3>{shade, 255, 255});
            } else
                pixel(x + col, y + row - first_row, {0, 0, 0});
        }
}
void menu(const TrainingView& view) {
    // Erase the JP word sprites completely. The guest still owns navigation,
    // settings and pause/resume; this replaces presentation alone.
    for (int y = 2; y < 143; ++y)
        for (int x = 4; x < 236; ++x)
            pixel(x, y, {16, 28, 43});
    constexpr std::array<int, 12> rows{24, 33, 42, 51, 60, 77, 86, 95, 104, 113, 122, 131};
    for (int y = rows[std::min(view.cursor, 11u)] - 1; y < rows[std::min(view.cursor, 11u)] + 8;
         ++y)
        for (int x = 12; x < 232; ++x)
            pixel(x, y, {37, 73, 101});
    // Native US headers, keeping the JP HP/PS order and settings meanings.
    word(0, 8, 6, true, 0, 45);
    word(0, 8, 51, false, 54, 8);
    word(0, 8, 60, false, 45, 8);
    word(0, 8, 68, true, 62, 9);
    word(0, 8, 77, false, 80, 8);
    word(0, 8, 86, false, 71, 8);
    word(0, 8, 95, false, 89, 8);
    word(1, 16, 104);
    const auto settings = view.options;
    constexpr unsigned action[]{3, 4, 5, 6, 14, 7, 8, 3};
    constexpr unsigned guard[]{9, 10, 11, 12};
    constexpr unsigned recovery[]{9, 13, 14, 15, 16, 9, 9, 9};
    constexpr unsigned gauges[]{14, 17, 14, 14};
    word(action[settings[0] & 7], 132, 24);
    word(guard[(settings[0] >> 3) & 3], 132, 33);
    word(recovery[settings[0] >> 5], 132, 42);
    word(gauges[(settings[1] >> 2) & 3], 132, 51);
    word(gauges[settings[1] & 3], 132, 60);
    word(gauges[settings[1] >> 6], 132, 77);
    word(gauges[(settings[1] >> 4) & 3], 132, 86);
    word((settings[2] & 3) ? 18 : 14, 132, 95);
}
} // namespace
void configure_training_tools(bool training, bool online, const std::filesystem::path& save,
                              const std::filesystem::path& cache) {
    allowed = training && !online;
    boxes_visible = false;
    pending = 0;
    busy = false;
    state_path = save;
    state_path += ".training.state";
    art = {};
    message.clear();
    message_until = 0;
    if (allowed) {
        const auto path = cache.empty() ? std::filesystem::path(SDL_GetBasePath()) / "assets" /
                                              "us-training-menu.bin"
                                        : cache;
        try {
            art.load(path);
        } catch (const std::exception&) { /* Headless oracles need no art. */
        }
    }
}
bool training_tools_allowed() {
    return allowed;
}
bool training_boxes_visible() {
    return allowed && boxes_visible;
}
TrainingView training_view() {
    return allowed ? TrainingView::sample(Memory{gbarecomp::active_bus()}) : TrainingView{};
}
bool training_tools_event(const SDL_Event& event, bool online) {
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return false;
    const auto key = event.key.scancode;
    // Retired shortcuts must not fall through to the engine's legacy slots.
    if (key == SDL_SCANCODE_F7 || key == SDL_SCANCODE_F8)
        return true;
    if (key != SDL_SCANCODE_F6 && key != SDL_SCANCODE_F2 && key != SDL_SCANCODE_F3)
        return false;
    // Reserved even outside Training: never leak into the engine's generic
    // F-key state slots, particularly during an online game.
    if (!online && allowed && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (key == SDL_SCANCODE_F6)
            boxes_visible = !boxes_visible;
        else
            training_request_state(key == SDL_SCANCODE_F3);
    }
    return true;
}
void training_request_state(bool load) {
    if (allowed)
        pending = load ? 2 : 1;
}
bool training_checkpoint_pending() {
    return allowed && (pending.load() != 0 || busy.load());
}
const std::string& training_tools_message() {
    if (!allowed || SDL_GetTicks() > message_until)
        message.clear();
    return message;
}
void training_presentation_restored() {
    display.fill(0);
}
bool training_service_checkpoint(gbarecomp::NativeFrameControl& machine) {
    if (!allowed || !pending.load())
        return false;
    busy = true;
    struct Completion {
        ~Completion() {
            busy = false;
        }
    } completion;
    const auto action = pending.exchange(0);
    if (!training_view().ready) {
        notice("Wait for the Training battle to start.");
        return false;
    }
    try {
        std::string error;
        std::vector<std::uint8_t> bytes;
        if (action == 1) {
            if (!machine.save(bytes, error))
                throw std::runtime_error(error);
            const std::array<char, 8> header{'S', 'E', 'E', 'D', 'T', 'R', 'N', '1'};
            const std::uint32_t crc = SDL_crc32(0, bytes.data(), bytes.size());
            auto temporary = state_path;
            temporary += ".pending";
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(header.data(), header.size());
            for (unsigned i = 0; i < 4; ++i)
                file.put(char(crc >> (i * 8)));
            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            file.close();
            if (file.fail() || !MoveFileExW(temporary.c_str(), state_path.c_str(),
                                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Could not write the Training state.");
            notice("Training state saved. F3: reload");
            return false;
        }
        std::ifstream file(state_path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("No Training state yet. Press F2 to save.");
        const auto size = file.tellg();
        if (size < 112 || size > 16 * 1024 * 1024)
            throw std::runtime_error("Invalid Training state file.");
        bytes.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            throw std::runtime_error("Could not read the Training state.");
        const std::array<char, 8> header{'S', 'E', 'E', 'D', 'T', 'R', 'N', '1'};
        if (!std::equal(header.begin(), header.end(), bytes.begin()))
            throw std::runtime_error("Unsupported Training state.");
        std::uint32_t crc = 0;
        for (unsigned i = 0; i < 4; ++i)
            crc |= std::uint32_t(bytes[8 + i]) << (i * 8);
        if (crc != SDL_crc32(0, bytes.data() + 12, bytes.size() - 12))
            throw std::runtime_error("Training state checksum failed.");
        bytes.erase(bytes.begin(), bytes.begin() + 12);
        if (!machine.load(bytes, error))
            throw std::runtime_error("State does not match this game, fighters, stage or colors.");
        release_host_controls();
        notice("Training state loaded.");
        return true;
    } catch (const std::exception& failure) {
        notice(failure.what());
        return false;
    }
}
const std::uint8_t* training_display_pixels(const std::uint8_t* pixels, unsigned w, unsigned h) {
    if (!allowed || !pixels || w != 240 || h != 160)
        return pixels;
    const auto view = training_view();
    if (!view.ready || (!view.paused && !boxes_visible))
        return pixels;
    if (view.paused && !art.ready())
        return pixels;
    std::copy_n(pixels, display.size(), display.begin());
    if (view.paused) {
        menu(view);
        return display.data();
    }
    for (const auto& box : view.boxes) {
        const std::array<std::uint8_t, 3> color = box.attack
                                                      ? std::array<std::uint8_t, 3>{255, 55, 70}
                                                      : std::array<std::uint8_t, 3>{60, 240, 115};
        for (int y = std::max(0, box.y); y < std::min(160, box.y + box.h); ++y)
            for (int x = std::max(0, box.x); x < std::min(240, box.x + box.w); ++x) {
                const bool edge =
                    x == box.x || x == box.x + box.w - 1 || y == box.y || y == box.y + box.h - 1;
                pixel(x, y, color, edge ? 255 : 35);
            }
    }
    return display.data();
}
} // namespace seed
