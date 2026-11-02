// Execute each displayed standalone command against the user's original game.
#include "jp_combat_runtime.h"
#include "move_list.h"
#include "training_tools.h"
#include "frame_control.h"
#include "runtime_arm.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
std::filesystem::path output;
void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
unsigned mask(std::string_view command) {
    unsigned out = 0;
    for (const auto c : command) {
        const auto index = std::string_view("abrlFBUD").find(c);
        require(index != std::string_view::npos, "Non-standalone input passed to native oracle");
        constexpr unsigned bits[]{1, 2, 256, 512, 16, 32, 64, 128};
        out |= bits[index];
    }
    return out;
}
int drive(gbarecomp::NativeFrameControl& machine) {
    namespace jp = seed::address::jp;
    const auto player = jp::kBattleContext + jp::kFighterRecordsOffset;
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    auto advance = [&](unsigned frames, unsigned input = 0) {
        for (unsigned i = 0; i < frames; ++i) {
            seed::set_jp_frame_inputs(input, 0);
            require(machine.advance(1023 ^ input), "Native movelist advance failed");
            machine.drain_audio(audio);
        }
    };
    std::ofstream report(output / "commands.csv");
    report << "fighter,form,air,input,expected_motion,observed_motion,observed_state,accepted\n";
    unsigned checked = 0, failed = 0;
    std::string error;
    for (unsigned fighter = 1; fighter <= 27; ++fighter) {
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.fighters[0] = fighter;
        request.fighters[1] = 8;
        const auto save = (output / "probe.sav").string();
        request.save = save.c_str();
        gbarecomp::RunOptions ignored;
        seed::configure_jp_combat(ignored, request);
        advance(600);
        require(seed::training_view().ready, "Native Training not ready for input verification");
        const auto type =
            bus_read_u8(jp::kFighterDescriptorTable + fighter * jp::kFighterDescriptorStride + 42);
        require(seed::fighter_moves(fighter)->berserker == (type <= 2   ? "SEED Mode"
                                                            : type == 3 ? "Newtype Mode"
                                                                        : "Normal Berserk"),
                "F1 Berserker category differs from the native descriptor");
        // Native cancel routes can require contact. Walk into melee range
        // rather than asserting that an on-hit chain also works on whiff.
        advance(100, 16);
        advance(8);
        std::vector<std::uint8_t> ms;
        require(machine.save(ms, error), error.c_str());
        for (unsigned form = 0; form < (fighter == 17 ? 2u : 1u); ++form) {
            require(machine.load(ms, error), error.c_str());
            if (form) {
                advance(1, 512);
                advance(150);
            }
            std::vector<std::uint8_t> ground;
            require(machine.save(ground, error), error.c_str());
            for (unsigned air = 0; air < 2; ++air) {
                require(machine.load(ground, error), error.c_str());
                if (air) {
                    advance(15, 64);
                    require(!(bus_read_u16(player + jp::kFighterFlagsOffset) & 4),
                            "Up did not produce a native air-command checkpoint");
                }
                std::vector<std::uint8_t> checkpoint;
                require(machine.save(checkpoint, error), error.c_str());
                for (const auto& move : seed::fighter_moves(fighter)->moves) {
                    if (air ? move.air == seed::MoveAir::Ground : move.air == seed::MoveAir::Only)
                        continue;
                    if (fighter == 17 && move.condition.starts_with(form ? "MS" : "MA"))
                        continue;
                    require(machine.load(checkpoint, error), error.c_str());
                    unsigned expected = 65535;
                    if (move.condition.find("Berserker active") != std::string_view::npos)
                        bus_write_u8(player + 2, fighter | 32);
                    bool accepted = true;
                    std::size_t at = 0;
                    do {
                        const auto end = move.command.find('>', at);
                        const auto bits = mask(move.command.substr(
                            at, end == std::string_view::npos ? end : end - at));
                        const auto descriptor = bus_read_u32(player + 28);
                        const auto records = bus_read_u32(descriptor);
                        const auto table = bus_read_u32(descriptor + (air ? 8 : 4));
                        const unsigned start =
                            at ? bus_read_u16(table + 4 * bus_read_u8(player + 89)) : 0;
                        unsigned target = 65535;
                        expected = 65535;
                        for (unsigned row = start;
                             row < 100 && bus_read_u16(table + row * 4) != 65535; ++row) {
                            const auto record = records + 20 * bus_read_u16(table + row * 4 + 2);
                            const auto required = bus_read_u16(record);
                            if ((bits & required) == required &&
                                (bits & 0x203) == (required & 0x203)) {
                                expected = bus_read_u16(record + 2);
                                target = row;
                                break;
                            }
                        }
                        bool step_accepted = false;
                        if (expected != 65535)
                            for (unsigned frame = 0; frame < (at ? 100u : 4u); ++frame) {
                                if (at)
                                    advance(1); // require a fresh press, including repeated buttons
                                const auto before_state = bus_read_u16(player);
                                if (at && before_state != 13 && before_state != 14)
                                    break;
                                const auto variant = bus_read_u8(player + 89);
                                const auto motion = bus_read_u8(player + 14);
                                const auto tick = bus_read_u16(player + 80);
                                auto raw = bits;
                                if (!(bus_read_u16(player + jp::kFighterFacingOffset) & 0x4000))
                                    raw = (bits & ~48u) | ((bits & 16u) << 1) | ((bits & 32u) >> 1);
                                advance(1, raw);
                                const auto state = bus_read_u16(player);
                                const bool restarted = !at || variant != bus_read_u8(player + 89) ||
                                                       motion != bus_read_u8(player + 14) ||
                                                       tick > bus_read_u16(player + 80);
                                step_accepted = bus_read_u8(player + 14) == expected && restarted &&
                                                (state == 13 || state == 14 || state == 20) &&
                                                (state == 20 || bus_read_u8(player + 89) == target);
                                if (step_accepted)
                                    break;
                            }
                        accepted &= step_accepted;
                        if (!step_accepted || end == std::string_view::npos)
                            break;
                        at = end + 1;
                    } while (at < move.command.size());
                    if (expected == 65535) {
                        ++failed;
                        report << fighter << ',' << form << ',' << air << ',' << move.command
                               << ",missing,0,0,false\n";
                        continue;
                    }
                    report << fighter << ',' << form << ',' << air << ',' << move.command << ','
                           << expected << ','
                           << unsigned(bus_read_u8(player + jp::kFighterMotionOffset)) << ','
                           << bus_read_u16(player) << ',' << (accepted ? "true" : "false") << '\n';
                    ++checked;
                    if (!accepted)
                        ++failed;
                }
            }
        }
        std::cout << "Verified native fighter " << fighter << '\n' << std::flush;
    }
    std::cout << "Native commands checked=" << checked << " failed=" << failed << '\n';
    require(!failed, "F1 command failed native execution; inspect commands.csv");
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4, "Pass JP ROM, BIOS and new E: output folder");
        output = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(output);
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
