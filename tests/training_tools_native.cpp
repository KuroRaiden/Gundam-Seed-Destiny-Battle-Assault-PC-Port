#include "jp_combat_runtime.h"
#include "training_tools.h"
#include "training_frame_meter.h"
#include "us_training_art.h"
#include "fighter_outline.h"
#include "frame_control.h"
#include "runtime_arm.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
std::filesystem::path output, font;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void key(SDL_Scancode sc) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = sc;
    seed::training_tools_event(event, false);
}
int drive(gbarecomp::NativeFrameControl& machine) {
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    auto advance = [&](unsigned frames, unsigned input = 0) {
        for (unsigned i = 0; i < frames; ++i) {
            seed::set_jp_frame_inputs(input, 0);
            require(machine.advance(1023 ^ input), "Native Training stopped");
            machine.drain_audio(audio);
        }
    };
    auto tap = [&](unsigned input) {
        advance(2, input);
        advance(2);
    };
    std::string error;
    std::vector<std::uint8_t> before, after;
    auto snapshot = [&](std::vector<std::uint8_t>& bytes) {
        require(machine.save(bytes, error), error.c_str());
    };
    auto save = output / "situation.sav";
    std::ofstream report(output / "cases.json");
    report << "[";
    for (unsigned fighter = 1; fighter <= 27; ++fighter) {
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.fighters[0] = fighter;
        request.fighters[1] = 8;
        const auto path = save.string();
        request.save = path.c_str();
        gbarecomp::RunOptions ignored;
        seed::configure_jp_combat(ignored, request);
        seed::configure_training_tools(true, false, save, font);
        advance(600);
        auto view = seed::training_view();
        require(view.ready && !view.paused, "Native Training not ready");
        require(!view.boxes.empty(), "Missing native hurtboxes");
        key(SDL_SCANCODE_F6);
        require(seed::training_boxes_visible(), "F6 did not enable boxes");
        unsigned hurt = 0, attack = 0;
        for (const auto& box : view.boxes)
            if (!box.attack)
                ++hurt;
        snapshot(before);
        const auto* raw = machine.pixels();
        std::vector<std::uint8_t> original(raw, raw + 240 * 160 * 3);
        seed::training_display_pixels(seed::outlined_combat_pixels(raw), 240, 160);
        snapshot(after);
        require(after == before, "Training presentation altered snapshot");
        require(std::equal(original.begin(), original.end(), machine.pixels()),
                "Training viewer modified native pixels");
        bool captured = false;
        for (auto input : {1u, 2u, 256u, 512u}) {
            for (unsigned i = 0; i < 55; ++i) {
                advance(1, i < 8 ? input : 0);
                view = seed::training_view();
                for (const auto& box : view.boxes)
                    if (box.attack)
                        ++attack;
                if (fighter == 13 && !captured &&
                    std::any_of(view.boxes.begin(), view.boxes.end(),
                                [](const auto& b) { return b.attack; })) {
                    const auto* pixels = seed::training_display_pixels(
                        seed::outlined_combat_pixels(machine.pixels()), 240, 160);
                    std::ofstream image(output / "hitboxes.rgb", std::ios::binary);
                    image.write(reinterpret_cast<const char*>(pixels), 240 * 160 * 3);
                    captured = true;
                }
            }
        }
        if (fighter > 1)
            report << ',';
        report << "{\"fighter\":" << fighter << ",\"hurtboxes\":" << hurt
               << ",\"attack_samples\":" << attack << ",\"snapshot_unchanged\":true}";
        report.flush();
        if (fighter == 13) {
            advance(10);
            key(SDL_SCANCODE_F2);
            require(seed::training_checkpoint_pending(), "F2 did not request a save");
            snapshot(before);
            require(!seed::training_service_checkpoint(machine), "Save reported a load");
            snapshot(after);
            require(after == before, "Saving changed the live machine");
            const auto saved_frame = machine.frame();
            advance(75, 16);
            advance(10, 2);
            key(SDL_SCANCODE_F3);
            require(seed::training_service_checkpoint(machine), "F3 failed to load");
            snapshot(after);
            require(after == before, "Quick load did not restore the complete native state");
            require(machine.frame() == saved_frame, "Quick load did not restore time");
            require(seed::training_frame_meter().size() == 0,
                    "Frame meter retained history from the future");
            for (unsigned repeat = 0; repeat < 15; ++repeat) {
                advance(9, repeat & 1 ? 32 : 16);
                key(SDL_SCANCODE_F3);
                require(seed::training_service_checkpoint(machine), "Repeated quick load failed");
                snapshot(after);
                require(after == before, "Repeated restore drifted");
            }
            advance(5);
            tap(8);
            require(seed::training_view().paused, "Native Start did not pause");
            auto capture = [&](const char* name) {
                const auto* pixels = seed::training_display_pixels(machine.pixels(), 240, 160);
                std::ofstream image(output / name, std::ios::binary);
                image.write(reinterpret_cast<const char*>(pixels), 240 * 160 * 3);
            };
            snapshot(before);
            capture("english-menu.rgb");
            snapshot(after);
            require(after == before, "Menu translation changed native state");
            key(SDL_SCANCODE_F2);
            seed::training_service_checkpoint(machine);
            snapshot(before);
            tap(8);
            advance(15);
            key(SDL_SCANCODE_F3);
            require(seed::training_service_checkpoint(machine), "Paused state did not load");
            snapshot(after);
            require(after == before && seed::training_view().paused, "Paused quick state failed");
            for (unsigned row = 0; row < 12; ++row) {
                const auto cursor = seed::training_view().cursor;
                require(cursor == row, "Translated pause cursor does not follow native row");
                if (row < 8) {
                    for (unsigned option = 0; option < 8; ++option) {
                        tap(16);
                        capture("menu-last.rgb");
                    }
                }
                tap(128);
            }
            require(seed::training_view().cursor == 0, "Training menu did not wrap");
            tap(8);
            require(!seed::training_view().paused, "Translated menu did not resume");
            advance(20);
            key(SDL_SCANCODE_F2);
            seed::training_service_checkpoint(machine);
            auto file = save;
            file += ".training.state";
            std::fstream damaged(file, std::ios::binary | std::ios::in | std::ios::out);
            damaged.seekg(-1, std::ios::end);
            char b;
            damaged.get(b);
            damaged.seekp(-1, std::ios::end);
            damaged.put(b ^ 1);
            damaged.close();
            snapshot(before);
            key(SDL_SCANCODE_F3);
            require(!seed::training_service_checkpoint(machine), "Corrupt state accepted");
            snapshot(after);
            require(after == before, "Rejected state partially changed the machine");
            key(SDL_SCANCODE_F2);
            seed::training_service_checkpoint(machine);
        }
        if (fighter == 14) {
            // The previous fighter's state must be rejected before any commit.
            snapshot(before);
            key(SDL_SCANCODE_F3);
            require(!seed::training_service_checkpoint(machine),
                    "Different fighter state accepted");
            snapshot(after);
            require(after == before, "Incompatible state mutated the machine");
        }
        std::cout << "PASS fighter " << fighter << ": " << hurt << " hurtboxes, " << attack
                  << " attack samples\n";
    }
    report << "]\n";
    seed::configure_training_tools(false, false, save, font);
    key(SDL_SCANCODE_F6);
    key(SDL_SCANCODE_F2);
    require(!seed::training_boxes_visible() && !seed::training_checkpoint_pending(),
            "VS enabled Training tools");
    seed::configure_training_tools(true, true, save, font);
    key(SDL_SCANCODE_F6);
    key(SDL_SCANCODE_F3);
    require(!seed::training_boxes_visible() && !seed::training_checkpoint_pending(),
            "Online enabled Training tools");
    std::cout << "PASS: US-font pause translation, native collision boxes, complete repeated quick "
                 "states, corrupt-file rejection, VS/online isolation.\n";
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: seed_training_tools_native JP_ROM BIOS US_ROM OUTPUT\n";
        return 2;
    }
    output = std::filesystem::absolute(argv[4]);
    std::filesystem::create_directories(output);
    font = output / "us-training-menu.bin";
    seed::UsTrainingArt::prepare(argv[3], font);
    seed::JpCombatRequest request;
    request.mode = seed::JpCombatMode::Training;
    gbarecomp::RunOptions options;
    seed::configure_jp_combat(options, request);
    options.native_frame_driver = drive;
    std::vector<std::string> values{
        argv[0], "--rom",      argv[1],  "--bios",
        argv[2], "--bios-hle", "--save", (output / "oracle.sav").string()};
    std::vector<char*> args;
    for (auto& s : values)
        args.push_back(s.data());
    return gbarecomp::run_game(int(args.size()), args.data(), options);
}
