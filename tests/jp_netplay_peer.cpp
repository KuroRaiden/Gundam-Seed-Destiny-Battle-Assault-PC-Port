#include "../src/game_addresses.h"
#include "jp_combat_runtime.h"
#include "frame_control.h"
#include "udp_transport.h"
#include "runtime_arm.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace jp_address = seed::address::jp;

namespace {
unsigned seat = 0, frames = 240, bind_port = 29070, peer_port = 29071;
std::filesystem::path report;
using Clock = std::chrono::steady_clock;
std::uint64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
        .count();
}
void require(bool value, const std::string& error) {
    if (!value)
        throw std::runtime_error(error);
}
void put32(std::uint8_t* data, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = std::uint8_t(value >> (8 * i));
}
std::uint32_t get32(const std::uint8_t* data) {
    return data[0] | (std::uint32_t(data[1]) << 8) | (std::uint32_t(data[2]) << 16) |
           (std::uint32_t(data[3]) << 24);
}
std::uint16_t local_input(unsigned player, unsigned frame) {
    if (frame < 40)
        return player ? 32 : 16;
    auto value = frame * 747796405u + player * 2891336453u;
    value ^= value >> 16;
    const unsigned action = (value >> 4) % 10;
    return std::uint16_t(action < 2    ? 1
                         : action < 3  ? 2
                         : action == 3 ? 256
                         : action == 4 ? 512
                                       : 0);
}
int drive(gbarecomp::NativeFrameControl& machine) {
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    for (unsigned frame = 0; frame < 600; ++frame) {
        seed::set_jp_frame_inputs(0, 0);
        require(machine.advance(1023), "Native JP bootstrap failed");
        machine.drain_audio(audio);
    }
    std::vector<std::uint8_t> state;
    std::string error;
    require(machine.save(state, error), error);
    const auto initial_crc = SDL_crc32(0, state.data(), state.size());
    seed::UdpTransport::Token token{};
    for (unsigned i = 0; i < token.size(); ++i)
        token[i] = std::uint8_t(i * 17 + 3);
    seed::UdpTransport transport(std::uint16_t(bind_port), "127.0.0.1", std::uint16_t(peer_port),
                                 token);
    bool peer_init = false, peer_ack = false, peer_done = false, peer_done_ack = false,
         started = false, submitted = false;
    std::uint32_t peer_crc = 0, final_crc = 0;
    unsigned next = 0, loads = 0, replays = 0, desyncs = 0;
    const auto begin = now();
    std::uint64_t last_init = 0, last_done = 0, stable_since = 0;
    auto send_control = [&](unsigned type, std::uint32_t crc) {
        std::array<std::uint8_t, 16> data{};
        data[0] = std::uint8_t(type);
        data[1] = std::uint8_t(seat);
        put32(data.data() + 4, frames);
        put32(data.data() + 8, crc);
        put32(data.data() + 12, 0x00020003u);
        transport.send_control(data);
    };
    auto control = [&] {
        for (const auto& data : transport.take_control()) {
            require(data.size() == 16 && data[1] == 1 - seat && get32(data.data() + 4) == frames &&
                        get32(data.data() + 12) == 0x00020003u,
                    "Peer qualification manifest mismatch");
            const auto crc = get32(data.data() + 8);
            if (data[0] == 1) {
                require(crc == initial_crc, "Native JP startup states differ");
                peer_init = true;
                send_control(2, crc);
            } else if (data[0] == 2) {
                require(crc == initial_crc, "Startup acknowledgement mismatch");
                peer_ack = true;
            } else if (data[0] == 3) {
                peer_done = true;
                peer_crc = crc;
                if (next == frames && final_crc == crc)
                    send_control(4, crc);
            } else if (data[0] == 4 && next == frames && crc == final_crc)
                peer_done_ack = true;
        }
    };
    while (!peer_init || !peer_ack) {
        if (now() - last_init >= 100) {
            send_control(1, initial_crc);
            last_init = now();
        }
        control();
        require(now() - begin < 15000, "UDP startup timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    GekkoSession* session = nullptr;
    require(gekko_create(&session, GekkoGameSession), "Cannot create Gekko network session");
    struct SessionOwner {
        GekkoSession*& session;
        ~SessionOwner() {
            gekko_destroy(&session);
        }
    } owner{session};
    GekkoConfig config{};
    config.num_players = 2;
    config.input_size = 2;
    config.state_size = unsigned(state.size() + 65536);
    config.max_input_prediction_window = 8;
    config.desync_detection = true;
    gekko_start(session, &config);
    gekko_net_adapter_set(session, transport.adapter());
    auto address = transport.address();
    int local = -1, remote = -1;
    for (unsigned p = 0; p < 2; ++p) {
        const int actor = gekko_add_actor(session, p == seat ? GekkoLocalPlayer : GekkoRemotePlayer,
                                          p == seat ? nullptr : &address);
        require(actor == int(p), "Gekko actor order mismatch");
        if (p == seat)
            local = actor;
        else
            remote = actor;
    }
    constexpr unsigned delay = 2;
    gekko_set_local_delay(session, local, delay);
    gekko_set_disconnect_timeout(session, 10000);
    auto deadline = Clock::now();
    bool converged = false;
    while (now() - begin < 90000) {
        transport.activate();
        control();
        if (started && !submitted && next + delay < frames) {
            auto input = local_input(seat, next);
            gekko_add_local_input(session, local, &input);
            submitted = true;
        }
        int count = 0;
        auto** events = gekko_update_session(session, &count);
        for (int i = 0; i < count; ++i) {
            const auto& event = *events[i];
            if (event.type == GekkoSaveEvent) {
                require(machine.save(state, error), error);
                require(state.size() <= config.state_size, "Native UDP snapshot capacity exceeded");
                std::memcpy(event.data.save.state, state.data(), state.size());
                *event.data.save.state_len = unsigned(state.size());
                *event.data.save.checksum = SDL_crc32(0, state.data(), state.size());
            } else if (event.type == GekkoLoadEvent) {
                state.assign(event.data.load.state,
                             event.data.load.state + event.data.load.state_len);
                require(machine.load(state, error), error);
                ++loads;
            } else if (event.type == GekkoAdvanceEvent) {
                const auto* input = reinterpret_cast<const std::uint16_t*>(event.data.adv.inputs);
                seed::set_jp_frame_inputs(input[0], input[1]);
                require(machine.advance(1023 ^ input[0]), "Native UDP frame failed");
                machine.drain_audio(audio);
                next = unsigned(event.data.adv.frame + 1);
                if (event.data.adv.rolling_back)
                    ++replays;
                else
                    submitted = false;
                require(next <= frames, "Gekko advanced beyond the test input horizon");
            }
        }
        auto** session_events = gekko_session_events(session, &count);
        for (int i = 0; i < count; ++i) {
            if (session_events[i]->type == GekkoSessionStarted)
                started = true;
            if (session_events[i]->type == GekkoPlayerDisconnected)
                throw std::runtime_error("Native UDP peer disconnected");
            if (session_events[i]->type == GekkoDesyncDetected) {
                ++desyncs;
                const auto& d = session_events[i]->data.desynced;
                std::fprintf(stderr, "Peer desync frame=%d local=%u remote=%u\n", d.frame,
                             d.local_checksum, d.remote_checksum);
            }
        }
        require(!desyncs, "Native peers reported a checksum mismatch");
        if (next == frames) {
            require(machine.save(state, error), error);
            final_crc = SDL_crc32(0, state.data(), state.size());
            if (now() - last_done >= 100) {
                send_control(3, final_crc);
                last_done = now();
                if (peer_done && peer_crc == final_crc)
                    send_control(4, final_crc);
            }
            if (peer_done && peer_done_ack && peer_crc == final_crc) {
                if (!stable_since)
                    stable_since = now();
                if (now() - stable_since >= 700) {
                    converged = true;
                    break;
                }
            } else
                stable_since = 0;
        }
        deadline += std::chrono::microseconds(16667);
        if (deadline < Clock::now() - std::chrono::milliseconds(50))
            deadline = Clock::now();
        std::this_thread::sleep_until(deadline);
    }
    GekkoRollbackStats rollback{};
    GekkoNetworkStats network{};
    gekko_rollback_stats(session, &rollback);
    gekko_network_stats(session, remote, &network);
    const auto packets = transport.counters();
    if (!report.empty()) {
        std::filesystem::create_directories(report.parent_path());
        std::ofstream out(report);
        out << "{\"engine\":\"native-jp\",\"transport\":\"udp\",\"backend\":\"GekkoNet\",\"seat\":"
            << seat << ",\"frames\":" << next << ",\"target\":" << frames
            << ",\"initial_crc\":" << initial_crc << ",\"final_crc\":" << final_crc
            << ",\"peer_crc\":" << peer_crc << ",\"converged\":" << (converged ? "true" : "false")
            << ",\"loads\":" << loads << ",\"replayed_frames\":" << replays
            << ",\"rollbacks\":" << rollback.total_count
            << ",\"max_rollback\":" << rollback.max_frames
            << ",\"stalls\":" << rollback.total_stalls << ",\"ping_ms\":" << network.avg_ping
            << ",\"jitter_ms\":" << network.jitter << ",\"packets_sent\":" << packets.sent
            << ",\"packets_received\":" << packets.received
            << ",\"packets_rejected\":" << packets.rejected << ",\"desyncs\":" << desyncs
            << ",\"hp\":[" << bus_read_u16(jp_address::kPlayer1Health) << ","
            << bus_read_u16(jp_address::kPlayer2Health) << "]}\n";
    }
    require(converged, "Native peers did not converge before timeout");
    std::printf("PASS UDP seat %u: %u frames, checksum %u, %u rollbacks, %u replayed frames\n",
                seat, next, final_crc, rollback.total_count, replays);
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::vector<char*> args{argv[0]};
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--seat" && i + 1 < argc)
                seat = std::stoul(argv[++i]);
            else if (flag == "--net-bind" && i + 1 < argc)
                bind_port = std::stoul(argv[++i]);
            else if (flag == "--net-peer" && i + 1 < argc)
                peer_port = std::stoul(argv[++i]);
            else if (flag == "--check-frames" && i + 1 < argc)
                frames = std::stoul(argv[++i]);
            else if (flag == "--report" && i + 1 < argc)
                report = argv[++i];
            else
                args.push_back(argv[i]);
        }
        require(seat < 2 && bind_port > 0 && bind_port <= 65535 && peer_port > 0 &&
                    peer_port <= 65535 && frames > 2,
                "Invalid native UDP qualification arguments");
        gbarecomp::RunOptions options;
        seed::JpCombatRequest request;
        request.fighters[0] = 13;
        request.fighters[1] = 14;
        seed::configure_jp_combat(options, request);
        options.native_frame_driver = drive;
        return gbarecomp::run_game(int(args.size()), args.data(), options);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "%s\n", failure.what());
        return 1;
    }
}
