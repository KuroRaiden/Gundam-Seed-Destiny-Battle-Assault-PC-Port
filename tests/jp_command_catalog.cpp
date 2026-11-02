// Development oracle: read the user's ROM in place and write research only to E:.
#include "jp_combat_runtime.h"
#include "frame_control.h"
#include "runtime_arm.h"
#include "training_tools.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

namespace {
std::filesystem::path output;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool readable(std::uint32_t at, unsigned size) {
    return (at >= 0x08000000 && at <= 0x08800000 - size) ||
           (at >= 0x02000000 && at <= 0x02040000 - size) ||
           (at >= 0x03000000 && at <= 0x03008000 - size);
}
int drive(gbarecomp::NativeFrameControl& machine) {
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    auto advance = [&](unsigned frames, unsigned input = 0) {
        for (unsigned i = 0; i < frames; ++i) {
            seed::set_jp_frame_inputs(input, 0);
            require(machine.advance(1023 ^ input), "Native catalog advance failed");
            machine.drain_audio(audio);
        }
    };
    std::ofstream file(output / "commands.json");
    file << "[";
    bool first_fighter = true;
    auto dump = [&](unsigned fighter, const char* form) {
        namespace jp = seed::address::jp;
        const auto player = jp::kBattleContext + jp::kFighterRecordsOffset;
        const auto descriptor = bus_read_u32(player + 28);
        require(readable(descriptor, 16), "Invalid native command descriptor");
        const auto commands = bus_read_u32(descriptor);
        require(readable(commands, 20), "Invalid native command records");
        if (!first_fighter)
            file << ',';
        first_fighter = false;
        file << "{\"fighter\":" << fighter << ",\"form\":\"" << form
             << "\",\"descriptor\":" << descriptor << ",\"player_bytes\":[";
        for (unsigned i = 0; i < 96; ++i) {
            if (i)
                file << ',';
            file << unsigned(bus_read_u8(player + i));
        }
        file << "],\"ground\":[";
        std::set<unsigned> used_commands;
        for (unsigned air = 0; air < 2; ++air) {
            if (air)
                file << "],\"air\":[";
            const auto table = bus_read_u32(descriptor + 4 + 4 * air);
            require(readable(table, 4), "Invalid native command route table");
            auto end = table + 1024;
            for (unsigned p = 0; p < 4; ++p) {
                const auto other = bus_read_u32(descriptor + 4 * p);
                if (other > table)
                    end = std::min(end, other);
            }
            std::queue<unsigned> pending;
            std::set<unsigned> starts;
            std::map<unsigned, std::pair<unsigned, unsigned>> rows;
            pending.push(0);
            while (!pending.empty()) {
                const auto start = pending.front();
                pending.pop();
                if (!starts.insert(start).second)
                    continue;
                if (table + start * 4 >= end)
                    continue;
                for (unsigned row = start; row < 256 && table + row * 4 < end; ++row) {
                    require(readable(table + row * 4, 4), "Command route escaped readable memory");
                    const auto next = bus_read_u16(table + row * 4);
                    if (next == 65535)
                        break;
                    const auto command = bus_read_u16(table + row * 4 + 2);
                    require(command < 512 && readable(commands + command * 20, 20),
                            "Invalid command record index");
                    rows[row] = {next, command};
                    used_commands.insert(command);
                    pending.push(next);
                }
            }
            bool first = true;
            for (const auto& [row, value] : rows) {
                if (!first)
                    file << ',';
                first = false;
                file << "{\"row\":" << row << ",\"next\":" << value.first
                     << ",\"command\":" << value.second << '}';
            }
        }
        file << "],\"commands\":[";
        bool first = true;
        for (const auto index : used_commands) {
            if (!first)
                file << ',';
            first = false;
            const auto at = commands + index * 20;
            file << "{\"index\":" << index << ",\"mask\":" << bus_read_u16(at)
                 << ",\"motion\":" << bus_read_u16(at + 2)
                 << ",\"ammo\":" << unsigned(bus_read_u8(at + 4))
                 << ",\"movement\":" << unsigned(bus_read_u8(at + 5)) << ",\"manual\":[";
            for (unsigned n = 0; n < 7; ++n) {
                if (n)
                    file << ',';
                file << bus_read_u16(at + 6 + n * 2);
            }
            file << "]}";
        }
        file << "],\"berserker_type\":"
             << unsigned(bus_read_u8(jp::kFighterDescriptorTable +
                                     fighter * jp::kFighterDescriptorStride + 42))
             << "}";
        file.flush();
    };
    for (unsigned fighter = 1; fighter <= 27; ++fighter) {
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.fighters[0] = fighter;
        request.fighters[1] = 8;
        const auto save = (output / "catalog.sav").string();
        request.save = save.c_str();
        gbarecomp::RunOptions ignored;
        seed::configure_jp_combat(ignored, request);
        advance(600);
        require(seed::training_view().ready, "Native Training did not initialize");
        dump(fighter, "MS");
        if (fighter == 1) {
            std::vector<std::uint8_t> checkpoint;
            std::string error;
            require(machine.save(checkpoint, error), error.c_str());
            const auto player =
                seed::address::jp::kBattleContext + seed::address::jp::kFighterRecordsOffset;
            std::ofstream probes(output / "direction-probes.json");
            probes << "[";
            bool first = true;
            for (auto input : {16u, 32u, 17u, 33u}) {
                require(machine.load(checkpoint, error), error.c_str());
                const auto x = std::int32_t(bus_read_u32(player + 36));
                advance(10, input);
                if (!first)
                    probes << ',';
                first = false;
                probes << "{\"input\":" << input
                       << ",\"dx\":" << std::int32_t(bus_read_u32(player + 36)) - x
                       << ",\"state\":" << bus_read_u16(player)
                       << ",\"motion\":" << unsigned(bus_read_u8(player + 14)) << "}";
            }
            probes << "]";
            require(machine.load(checkpoint, error), error.c_str());
        }
        if (fighter == 17) {
            advance(1, 512);
            advance(150);
            dump(fighter, "after-ground-L");
        }
        std::cout << "Cataloged native fighter " << fighter << '\n' << std::flush;
    }
    file << "]\n";
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4, "Pass JP ROM, BIOS and an E: output folder");
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
        for (auto& value : values)
            args.push_back(value.data());
        return gbarecomp::run_game(int(args.size()), args.data(), options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
