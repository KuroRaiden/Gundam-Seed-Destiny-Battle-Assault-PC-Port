#include "jp_combat_runtime.h"
#include "frame_control.h"
#include "gekkonet.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned frames = 360;
std::filesystem::path report;
std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
void require(bool ok, const char* error) {
    if (!ok)
        throw std::runtime_error(error);
}
unsigned check_rejected_snapshots(gbarecomp::NativeFrameControl& machine,
                                  const std::vector<std::uint8_t>& original) {
    auto u32 = [&](std::size_t at) {
        require(at + 4 <= original.size(), "Snapshot test header is truncated");
        return original[at] | (unsigned(original[at + 1]) << 8) |
               (unsigned(original[at + 2]) << 16) | (unsigned(original[at + 3]) << 24);
    };
    const std::size_t stack = 88 + u32(84);
    const std::size_t swi = stack + 4 + 4 * u32(stack);
    const std::size_t devices = swi + 4 + 8 * u32(swi);
    const std::size_t mods = devices + 4 + u32(devices);
    // The canonical device stream starts with five fixed memory regions,
    // followed by the last fetched word and the BIOS-access boolean.
    const auto device_boolean = devices + 4 + 0x40000 + 0x8000 + 0x400 + 0x18000 + 0x400 + 4;
    require(mods + 4 + u32(mods) == original.size(), "Unexpected native snapshot layout");
    unsigned rejected = 0;
    auto reject = [&](const std::vector<std::uint8_t>& broken) {
        std::string error;
        if (machine.load(broken, error))
            throw std::runtime_error("Malformed snapshot case " + std::to_string(rejected) +
                                     " was accepted");
        require(!error.empty(), "Rejected snapshot has no diagnostic");
        std::vector<std::uint8_t> after;
        require(machine.save(after, error), error.c_str());
        require(after == original, "Rejected snapshot changed the live simulation");
        ++rejected;
    };
    for (const auto size : {std::size_t(0), std::size_t(99), devices + 4, original.size() - 1})
        reject(std::vector<std::uint8_t>(original.begin(), original.begin() + size));
    for (const auto offset :
         {std::size_t(0), std::size_t(4), device_boolean, original.size() - 1}) {
        auto broken = original;
        broken[offset] ^= 0xFF;
        reject(broken);
    }
    for (const auto offset : {std::size_t(84), stack, swi, devices, mods}) {
        auto broken = original;
        std::fill_n(broken.begin() + offset, 4, 0xFF);
        reject(broken);
    }
    auto trailing = original;
    trailing.push_back(0);
    reject(trailing);
    std::string error;
    require(machine.load(original, error), error.c_str());
    std::vector<std::uint8_t> roundtrip;
    require(machine.save(roundtrip, error), error.c_str());
    require(roundtrip == original, "Native snapshot round trip changed machine state");
    return rejected;
}
int drive(gbarecomp::NativeFrameControl& machine) {
    machine.seed_clock(946684800);
    for (unsigned i = 0; i < 600; ++i) {
        seed::set_jp_frame_inputs(0, 0);
        require(machine.advance(1023), "JP bootstrap stopped");
        machine.drain_audio(audio);
    }
    std::string error;
    std::vector<std::uint8_t> state;
    require(machine.save(state, error), error.c_str());
    const auto rejected_snapshots = check_rejected_snapshots(machine, state);
    const unsigned capacity = unsigned(state.size() + 65536);
    GekkoSession* session = nullptr;
    require(gekko_create(&session, GekkoStressSession), "Cannot create Gekko stress session");
    struct SessionOwner {
        GekkoSession*& session;
        ~SessionOwner() {
            gekko_destroy(&session);
        }
    } owner{session};
    GekkoConfig config{};
    config.num_players = 2;
    config.input_size = 2;
    config.state_size = capacity;
    config.desync_detection = true;
    config.check_distance = 8;
    gekko_start(session, &config);
    int actors[2]{gekko_add_actor(session, GekkoLocalPlayer, nullptr),
                  gekko_add_actor(session, GekkoLocalPlayer, nullptr)};
    require(actors[0] >= 0 && actors[1] >= 0, "Cannot create local stress actors");
    unsigned saves = 0, loads = 0, advances = 0, replays = 0, desyncs = 0, random = 0x53454544;
    bool last_replay = false, dumped = false;
    std::map<int, std::vector<std::uint8_t>> first_states;
    auto check_session = [&] {
        int count = 0;
        auto** events = gekko_session_events(session, &count);
        for (int i = 0; i < count; ++i)
            if (events[i]->type == GekkoDesyncDetected) {
                const auto& d = events[i]->data.desynced;
                ++desyncs;
                std::fprintf(stderr, "DESYNC frame=%d local=%u replay=%u\n", d.frame,
                             d.local_checksum, d.remote_checksum);
            }
    };
    for (unsigned frame = 0; frame < frames; ++frame) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        std::uint16_t inputs[2]{
            std::uint16_t(((frame / 40) % 2 ? 16 : 32) |
                          ((random & 3) == 0 ? 1u << ((random >> 8) % 2) : 0)),
            std::uint16_t(((frame / 35) % 2 ? 32 : 16) |
                          ((random & 12) == 0 ? 1u << ((random >> 10) % 2) : 0))};
        for (unsigned p = 0; p < 2; ++p)
            gekko_add_local_input(session, actors[p], &inputs[p]);
        int count = 0;
        auto** events = gekko_update_session(session, &count);
        for (int i = 0; i < count; ++i) {
            const auto& event = *events[i];
            if (event.type == GekkoSaveEvent) {
                require(machine.save(state, error), error.c_str());
                require(state.size() <= capacity, "Snapshot capacity exceeded");
                if (event.data.save.frame <= 10) {
                    const int f = event.data.save.frame;
                    if (!last_replay)
                        first_states[f] = state;
                    else if (!dumped && first_states.contains(f) && first_states[f] != state &&
                             !report.empty()) {
                        std::filesystem::create_directories(report.parent_path());
                        auto dump = [&](const char* name, const std::vector<std::uint8_t>& bytes) {
                            std::ofstream file(report.parent_path() / name, std::ios::binary);
                            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                        };
                        dump("expected-native.state", first_states[f]);
                        dump("replayed-native.state", state);
                        std::fprintf(stderr, "First snapshot mismatch at frame %d\n", f);
                        dumped = true;
                    }
                }
                *event.data.save.state_len = unsigned(state.size());
                *event.data.save.checksum = SDL_crc32(0, state.data(), state.size());
                std::memcpy(event.data.save.state, state.data(), state.size());
                ++saves;
            } else if (event.type == GekkoLoadEvent) {
                state.assign(event.data.load.state,
                             event.data.load.state + event.data.load.state_len);
                require(machine.load(state, error), error.c_str());
                ++loads;
            } else if (event.type == GekkoAdvanceEvent) {
                last_replay = event.data.adv.rolling_back;
                const auto* keys = reinterpret_cast<const std::uint16_t*>(event.data.adv.inputs);
                seed::set_jp_frame_inputs(keys[0], keys[1]);
                require(machine.advance(1023 ^ keys[0]), "JP replay stopped");
                machine.drain_audio(audio);
                ++advances;
                if (event.data.adv.rolling_back)
                    ++replays;
            }
        }
        check_session();
        if (desyncs)
            break;
        if (frame % 60 == 59)
            std::printf("Rollback stress: %u frames, %u loads, %u replays\n", frame + 1, loads,
                        replays);
    }
    check_session();
    if (!report.empty()) {
        std::filesystem::create_directories(report.parent_path());
        std::ofstream out(report);
        out << "{\"backend\":\"GekkoNet\",\"engine\":\"native-jp\",\"requested_frames\":" << frames
            << ",\"snapshot_capacity\":" << capacity << ",\"saves\":" << saves
            << ",\"loads\":" << loads << ",\"advances\":" << advances << ",\"replays\":" << replays
            << ",\"rejected_snapshots\":" << rejected_snapshots << ",\"desyncs\":" << desyncs
            << "}\n";
    }
    require(!desyncs, "Native JP snapshot replay diverged");
    require(loads > 0 && replays > 0, "Stress test never exercised rollback");
    std::puts("PASS: GekkoNet stress session restored and replayed the native JP simulation "
              "without checksum differences.");
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::vector<char*> args{argv[0]};
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--check-frames" && i + 1 < argc)
                frames = std::stoul(argv[++i]);
            else if (flag == "--report" && i + 1 < argc)
                report = argv[++i];
            else
                args.push_back(argv[i]);
        }
        gbarecomp::RunOptions options;
        seed::JpCombatRequest request;
        request.fighters[0] = 13;
        request.fighters[1] = 14;
        seed::configure_jp_combat(options, request);
        options.native_frame_driver = drive;
        return gbarecomp::run_game(int(args.size()), args.data(), options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
