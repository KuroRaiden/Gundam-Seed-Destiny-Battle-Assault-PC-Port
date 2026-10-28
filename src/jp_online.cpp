#include "game_addresses.h"
#include "jp_online.h"
#include "frame_control.h"
#include "host_window.h"
#include "host_overlay.h"
#include "online_room.h"
#include "player_profile.h"
#include "runtime_arm.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace jp_address = seed::address::jp;
namespace seed {
namespace {
using Clock = std::chrono::steady_clock;
JpCombatRequest match;
std::string banner;
std::uint64_t now() {
    return SDL_GetTicks();
}
void require(bool ok, const std::string& error) {
    if (!ok)
        throw std::runtime_error(error);
}
void put32(std::uint8_t* p, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = std::uint8_t(n >> (8 * i));
}
std::uint32_t get32(const std::uint8_t* p) {
    return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}
void overlay(gbarecomp::HostOverlay* out) {
    if (banner.empty())
        return;
    using namespace gbarecomp;
    out->fill_rect(0, 0, 240, 17, 0, {5, 12, 25, 230}, OverlaySpace::View);
    out->text(120, 3, 10, {255, 255, 255, 255}, banner.c_str(), OverlayAlign::Center,
              OverlaySpace::View);
}
int drive(gbarecomp::NativeFrameControl& machine) {
    OnlineCombatResult result;
    auto publish = [&](const char* message) {
        std::snprintf(result.message, sizeof(result.message), "%s", message);
        if (match.online_result)
            *match.online_result = result;
    };
    try {
        require(match.connection.seat < 2 && match.connection.epoch,
                "Invalid online battle contract");
        UdpTransport transport(match.connection);
        const auto seat = match.connection.seat;
        const std::array<std::string, 2> players{normalize_player_name(match.player_names[0]),
                                                 normalize_player_name(match.player_names[1])};
        constexpr unsigned input_delay = 2;
        auto title = [&](unsigned rollback, float ping) {
            if (match.surface.window)
                SDL_SetWindowTitle(
                    static_cast<SDL_Window*>(match.surface.window),
                    online_match_title(players, input_delay, rollback, ping).c_str());
        };
        const auto configured_target = match.qualification_frames;
        unsigned target = configured_target;
        gbarecomp::HostWindow window;
        if (match.surface.window) {
            require(window.open(4, 240, 160, "Gundam SEED Destiny / Battle Assault - Online VS"),
                    "Cannot borrow the game window");
            window.load_input_config(SDL_GetBasePath(), false);
            window.set_network_session(true);
            window.set_host_overlay(overlay);
            title(0, 0);
        }
        auto pump = [&] {
            if (!window.is_open())
                return;
            const auto events = window.pump();
            if (events.toggle_fullscreen)
                window.set_fullscreen(window.fullscreen() ? 0 : 1);
            if (events.window_bigger)
                window.adjust_scale(1);
            if (events.window_smaller)
                window.adjust_scale(-1);
            if (events.volume_up)
                window.set_volume(window.volume() + 5);
            if (events.volume_down)
                window.set_volume(window.volume() - 5);
            if (events.toggle_fps)
                window.set_fps_readout(!window.fps_readout());
            require(!events.quit, "Left online match");
        };
        machine.seed_clock(946684800);
        std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
        banner = "Preparing JP combat...";
        unsigned startup_frames = 0;
        for (; startup_frames < 600; ++startup_frames) {
            set_jp_frame_inputs(0, 0);
            require(machine.advance(1023), "JP startup stopped");
            machine.drain_audio(audio);
            if (startup_frames % 16 == 0)
                pump();
            if (jp_combat_startup_ready()) {
                ++startup_frames;
                break;
            }
        }
        require(jp_combat_startup_ready(), "JP battle initialization did not finish");
        std::fprintf(stderr, "online_startup: ready before round intro after %u native frames\n",
                     startup_frames);
        result.stage =
            bus_read_u8(jp_address::kBattleContext + jp_address::kBattleStageOffset) & 31;
        require(result.stage == match.stage, "JP combat loaded a different stage");
        std::vector<std::uint8_t> state;
        std::string error;
        require(machine.save(state, error), error);
        result.initial_crc = SDL_crc32(0, state.data(), state.size());
        bool peer_init = false, peer_ack = false, peer_done = false, peer_done_ack = false,
             started = false, submitted = false;
        unsigned next = 0;
        std::uint64_t last_init = 0, last_done = 0, last_end = 0, stable_since = 0;
        const auto began = now();
        auto send = [&](unsigned type, std::uint32_t crc) {
            std::array<std::uint8_t, kCombatControlPacketBytes> p{};
            put32(p.data(), kCombatControlMagic);
            put32(p.data() + 4, kRoomProtocol);
            p[8] = std::uint8_t(type);
            p[9] = std::uint8_t(seat);
            p[10] = match.fighters[0];
            p[11] = match.fighters[1];
            put32(p.data() + 12, match.connection.epoch);
            put32(p.data() + 16, crc);
            put32(p.data() + 20, configured_target);
            p[24] = match.colors[0];
            p[25] = match.colors[1];
            p[28] = match.stage;
            for (unsigned seat = 0; seat < 2; ++seat) {
                p[26 + seat] = match.palettes[seat].enabled;
                encode_palette(p.data() + 32 + seat * kMsPaletteBytes, match.palettes[seat]);
            }
            transport.send_control(p);
        };
        auto control = [&] {
            for (const auto& p : transport.take_control()) {
                if (acknowledge_room_start(transport, p, match.connection, match.fighters,
                                           match.colors, match.palettes, match.stage))
                    continue;
                if (p.size() == 32 && get32(p.data()) == kRoomMagic &&
                    get32(p.data() + 4) == kRoomProtocol && p[9] == 1 - seat && p[8] == 5 &&
                    (get32(p.data() + 20) == match.connection.epoch ||
                     get32(p.data() + 20) == next_match_epoch(match.connection.epoch)))
                    throw std::runtime_error("Opponent left online room");
                if (p.size() != kCombatControlPacketBytes ||
                    get32(p.data()) != kCombatControlMagic || p[9] != 1 - seat ||
                    get32(p.data() + 12) != match.connection.epoch)
                    continue;
                require(get32(p.data() + 4) == kRoomProtocol && p[10] == match.fighters[0] &&
                            p[11] == match.fighters[1] && p[24] == match.colors[0] &&
                            p[25] == match.colors[1] && p[28] == match.stage &&
                            get32(p.data() + 20) == configured_target,
                        "Opponent uses a different online battle configuration");
                for (unsigned seat = 0; seat < 2; ++seat) {
                    CustomPalette palette;
                    require(decode_palette(p.data() + 32 + seat * kMsPaletteBytes, p[26 + seat],
                                           palette) &&
                                palette == match.palettes[seat],
                            "Opponent uses different MS colors");
                }
                const auto crc = get32(p.data() + 16);
                if (p[8] == 1) {
                    require(crc == result.initial_crc,
                            "JP starting states differ; use the same game build");
                    peer_init = true;
                    send(2, crc);
                } else if (p[8] == 2) {
                    require(crc == result.initial_crc, "JP startup acknowledgement differs");
                    peer_ack = true;
                } else if (p[8] == 3 && target) {
                    peer_done = true;
                    result.peer_crc = crc;
                    if (next == target && result.final_crc == crc)
                        send(4, crc);
                } else if (p[8] == 4 && target && next == target && crc == result.final_crc)
                    peer_done_ack = true;
                else if (p[8] == 5)
                    throw std::runtime_error("Opponent left online match");
                else if (p[8] == 6 && !configured_target && crc && crc <= next + 600) {
                    // Finish on a common future input boundary. A result must
                    // survive the prediction window before returning to menus.
                    if (!target || crc > target) {
                        target = crc > next + 16 ? crc : next + 90;
                        peer_done = peer_done_ack = false;
                        stable_since = 0;
                        last_end = 0;
                    }
                    send(7, target);
                } else if (p[8] == 7 && !configured_target && target && crc > target &&
                           crc <= next + 600) {
                    target = crc;
                    peer_done = peer_done_ack = false;
                    stable_since = 0;
                    last_end = 0;
                }
            }
        };
        banner = "Connecting / checking JP state...";
        while (!peer_init || !peer_ack) {
            if (now() - last_init >= 80) {
                send(1, result.initial_crc);
                last_init = now();
            }
            control();
            pump();
            if (window.is_open())
                window.present(machine.pixels());
            require(now() - began < 30000, "Online startup timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        GekkoSession* session = nullptr;
        require(gekko_create(&session, GekkoGameSession), "Cannot create rollback session");
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
            const int actor =
                gekko_add_actor(session, p == seat ? GekkoLocalPlayer : GekkoRemotePlayer,
                                p == seat ? nullptr : &address);
            require(actor == int(p), "Rollback actor order differs");
            if (p == seat)
                local = actor;
            else
                remote = actor;
        }
        constexpr unsigned delay = input_delay;
        gekko_set_local_delay(session, local, delay);
        gekko_set_disconnect_timeout(session, 10000);
        auto session_events = [&] {
            int count = 0;
            auto** events = gekko_session_events(session, &count);
            for (int i = 0; i < count; ++i) {
                if (events[i]->type == GekkoSessionStarted)
                    started = true;
                if (events[i]->type == GekkoPlayerDisconnected) {
                    std::fprintf(stderr,
                                 "online_disconnect_state: seat=%u frame=%u target=%u done=%u "
                                 "ack=%u crc=%u peer_crc=%u\n",
                                 seat, next, target, unsigned(peer_done), unsigned(peer_done_ack),
                                 result.final_crc, result.peer_crc);
                    throw std::runtime_error("Opponent disconnected");
                }
                if (events[i]->type == GekkoDesyncDetected) {
                    ++result.desyncs;
                    throw std::runtime_error("Rollback checksum mismatch; match stopped");
                }
            }
        };
        auto deadline = Clock::now();
        bool converged = false;
        std::uint64_t finish_since = configured_target ? began : 0;
        unsigned stalled_ticks = 0;
        std::uint64_t last_title = 0;
        struct LeaveNotice {
            decltype(send)& transmit;
            bool& complete;
            ~LeaveNotice() {
                if (!complete)
                    for (unsigned i = 0; i < 3; ++i)
                        transmit(5, 0);
            }
        } leave_notice{send, converged};
        while (!finish_since || now() - finish_since < 90000) {
            pump();
            control();
            transport.activate();
            if (!configured_target && !target &&
                (jp_combat_ended() ||
                 (match.qualification_end_frame && next >= match.qualification_end_frame)))
                target = next + 90;
            if (target && !finish_since)
                finish_since = now();
            if (!configured_target && target && now() - last_end >= 80) {
                send(6, target);
                last_end = now();
            }
            if (started && !submitted && (!target || next + delay < target)) {
                std::uint16_t players[2]{};
                if (match.sample_inputs)
                    match.sample_inputs(players);
                // Native Start pauses both machines. Online remapping uses F4
                // and continues stepping; a local pause cannot stop the peer.
                auto input = std::uint16_t(players[0] & 1023u & ~8u);
                gekko_add_local_input(session, local, &input);
                submitted = true;
            }
            const auto before = next;
            int count = 0;
            auto** events = gekko_update_session(session, &count);
            bool advanced = false;
            for (int i = 0; i < count; ++i) {
                const auto& event = *events[i];
                if (event.type == GekkoSaveEvent) {
                    require(machine.save(state, error), error);
                    require(state.size() <= config.state_size,
                            "Rollback snapshot capacity exceeded");
                    std::memcpy(event.data.save.state, state.data(), state.size());
                    *event.data.save.state_len = unsigned(state.size());
                    *event.data.save.checksum = SDL_crc32(0, state.data(), state.size());
                } else if (event.type == GekkoLoadEvent) {
                    state.assign(event.data.load.state,
                                 event.data.load.state + event.data.load.state_len);
                    require(machine.load(state, error), error);
                } else if (event.type == GekkoAdvanceEvent) {
                    const auto* input =
                        reinterpret_cast<const std::uint16_t*>(event.data.adv.inputs);
                    set_jp_frame_inputs(input[0], input[1]);
                    require(machine.advance(1023 ^ input[0]), "JP online frame stopped");
                    machine.drain_audio(audio);
                    if (event.data.adv.rolling_back)
                        ++result.replayed_frames;
                    else {
                        submitted = false;
                        advanced = true;
                        if (window.is_open())
                            for (const auto& block : audio)
                                window.push_audio_block(block.samples.data(), block.samples.size(),
                                                        block.rate);
                    }
                    next = unsigned(event.data.adv.frame + 1);
                    require(!target || next <= target, "Rollback crossed qualification horizon");
                }
            }
            session_events();
            stalled_ticks = advanced ? 0 : stalled_ticks + 1;
            if (target && next == target) {
                require(machine.save(state, error), error);
                result.final_crc = SDL_crc32(0, state.data(), state.size());
                if (now() - last_done >= 80) {
                    send(3, result.final_crc);
                    last_done = now();
                    if (peer_done && result.peer_crc == result.final_crc)
                        send(4, result.final_crc);
                }
                if (peer_done && peer_done_ack && result.peer_crc == result.final_crc) {
                    if (!stable_since)
                        stable_since = now();
                    if (now() - stable_since >= 700) {
                        converged = true;
                        break;
                    }
                } else
                    stable_since = 0;
            }
            GekkoRollbackStats rollback{};
            GekkoNetworkStats network{};
            gekko_rollback_stats(session, &rollback);
            gekko_network_stats(session, remote, &network);
            result.frames = next;
            result.rollbacks = rollback.total_count;
            result.max_rollback = rollback.max_frames;
            result.stalls = rollback.total_stalls;
            result.ping_ms = network.avg_ping;
            result.jitter_ms = network.jitter;
            if (now() - last_title >= 250) {
                title(rollback.last_frames, network.avg_ping);
                last_title = now();
            }
            banner = !started             ? "Synchronizing rollback..."
                     : stalled_ticks > 15 ? "Waiting for opponent..."
                                          : "";
            if (window.is_open())
                window.present(machine.pixels());
            // Poll transport during the frame wait instead of adding another
            // full frame to every network acknowledgement. Poll events must
            // be consumed before UpdateSession clears their event buffer.
            deadline += std::chrono::microseconds(16743);
            if (deadline < Clock::now() - std::chrono::milliseconds(50))
                deadline = Clock::now();
            do {
                transport.activate();
                gekko_network_poll(session);
                session_events();
                control();
                if (Clock::now() >= deadline)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (true);
        }
        result.frames = next;
        result.converged = converged;
        result.hp[0] = bus_read_u16(jp_address::kPlayer1Health);
        result.hp[1] = bus_read_u16(jp_address::kPlayer2Health);
        require(!target || converged, "Online qualification did not converge");
        publish("Online match finished");
        return 0;
    } catch (const std::exception& error) {
        publish(error.what());
        std::fprintf(stderr, "jp_online: %s\n", error.what());
        return (match.qualification_frames || match.qualification_end_frame) ? 1 : 0;
    }
}
} // namespace
void configure_jp_online(gbarecomp::RunOptions& options, const JpCombatRequest& request) {
    match = request;
    banner.clear();
    options.native_frame_driver = drive;
}
} // namespace seed
