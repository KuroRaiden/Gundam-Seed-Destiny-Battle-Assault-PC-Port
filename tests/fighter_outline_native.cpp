#include "fighter_outline.h"
#include "jp_combat_runtime.h"
#include "game_addresses.h"
#include "frame_control.h"
#include "runtime_arm.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
std::filesystem::path output;
bool training_only = false;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int drive(gbarecomp::NativeFrameControl& machine) {
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    std::vector<std::uint8_t> before, after;
    std::string error;
    std::ofstream report(output / "cases.json"); report << "[";
    unsigned cases = 0;
    auto advance = [&](unsigned frames, unsigned p1 = 0, unsigned p2 = 0) {
        for (unsigned n = 0; n < frames; ++n) {
            seed::set_jp_frame_inputs(p1, p2);
            require(machine.advance(1023), "Native frame stopped");
            machine.drain_audio(audio);
        }
    };
    for (unsigned fighter = training_only ? 13 : 1; fighter <= (training_only ? 13u : 27u); ++fighter) {
        seed::JpCombatRequest request;
        request.fighters[0] = fighter; request.fighters[1] = fighter % 27 + 1;
        if (training_only) request.mode = seed::JpCombatMode::Training;
        gbarecomp::RunOptions ignored;
        seed::configure_jp_combat(ignored, request);
        advance(600);
        const auto* raw = machine.pixels();
        std::vector<std::uint8_t> original(raw, raw + 240 * 160 * 3);
        require(machine.save(before, error), error.c_str());
        const auto* display = seed::outlined_combat_pixels(raw);
        const auto stats = seed::fighter_outline_stats();
        require(stats.fighter_pixels[0] > 10 && stats.fighter_pixels[1] > 10,
                "A native fighter body was missing from the outline mask");
        require(stats.black_pixels > 10, "Native outline was not drawn");
        require(std::equal(original.begin(), original.end(), machine.pixels()), "Native pixels changed");
        require(machine.save(after, error) && after == before, "Outline altered native snapshot/checksum");
        auto write = [&](const char* name, const std::uint8_t* rgb) {
            std::ofstream file(output / (std::to_string(fighter) + name), std::ios::binary);
            file.write(reinterpret_cast<const char*>(rgb), 240 * 160 * 3);
        };
        write("-raw.rgb", raw); write("-outlined.rgb", display);
        if (cases++) report << ",";
        report << "{\"fighter\":" << fighter << ",\"p1_pixels\":" << stats.fighter_pixels[0]
               << ",\"p2_pixels\":" << stats.fighter_pixels[1] << ",\"black_pixels\":"
               << stats.black_pixels << ",\"snapshot_unchanged\":true}";
        // Restore drops presentation history; the next full native frame rebuilds it.
        require(machine.load(before, error), error.c_str());
        require(seed::outlined_combat_pixels(machine.pixels()) == machine.pixels(), "Stale restore mask");
        advance(1);
        seed::outlined_combat_pixels(machine.pixels());
        require(seed::fighter_outline_stats().black_pixels > 10, "Outline did not recover after restore");
        // Exercise movement and A/B/L/R without touching game palettes or state.
        for (const auto input : {16u, 1u, 2u, 256u, 512u}) {
            advance(3); advance(20, input, input == 16 ? 32 : input);
            seed::outlined_combat_pixels(machine.pixels());
        }
        std::printf("PASS: native fighters %u / %u, %u black pixels, unchanged snapshot\n",
                    fighter, request.fighters[1], stats.black_pixels); std::fflush(stdout);
    }
    report << "]\n";
    return 0;
}
}
int main(int argc, char** argv) {
    try {
        std::vector<char*> args{argv[0]};
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--training") training_only = true;
            else if (std::string(argv[i]) == "--out" && i + 1 < argc) output = argv[++i];
            else args.push_back(argv[i]);
        require(!output.empty(), "Specify --out");
        std::filesystem::create_directories(output);
        seed::JpCombatRequest request;
        gbarecomp::RunOptions options;
        seed::configure_jp_combat(options, request);
        options.native_frame_driver = drive;
        return gbarecomp::run_game(args.size(), args.data(), options);
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
