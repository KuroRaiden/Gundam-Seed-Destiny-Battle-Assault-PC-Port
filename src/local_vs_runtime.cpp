#include "game_addresses.h"
#include "local_vs_runtime.h"
#include "local_vs.h"
#include "input_bindings.h"
#include "jp_combat.h"
#include "ms_color_profiles.h"
#include "main_menu_label.h"
#include "online_lobby.h"
#include "online_room.h"
#include "invite_window.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "jp_selection.h"
#include "jp_selection_overlay.h"
#include "runtime_arm.h"
#include "mod_function_hooks.h"
#include "touch_input.h"
#include "host_overlay.h"
#include "mini_json.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace gba_address = seed::address::common;
namespace us_address = seed::address::us;

namespace seed {
namespace {
constexpr const char* kInputHook = "seed.two-local-inputs";
constexpr const char* kSelectHook = "seed.two-local-selections";
constexpr const char* kMapHook = "seed.local-fighter-input";
constexpr const char* kSelectBeginHook = "seed.local-select-begin";
bool selector_active = false;
MatchInputs inputs;
LocalVsSelection selection;
MsColorProfiles ms_colors;
VsMenuRoute menu_route;
MenuBattleMode battle_mode = MenuBattleMode::None;
OnlineLobbyState online_lobby;
OnlineRoom online_room;
OnlineCombatResult last_online_result;
unsigned online_qualification_frames = 0, online_qualification_end_frame = 0;
std::string pending_invite;
unsigned invite_nav_frames = 0;
void leave_select();
JpSelectionArt jp_art;
std::array<std::uint16_t, 2> sampled_player_inputs{};
std::atomic<bool> scripted_input_enabled{false};
std::atomic<std::uint32_t> scripted_player_inputs{0};
std::string jp_rom_path, bios_path, jp_save_path;
std::filesystem::path combat_module_path;
int jp_debug_port = 0;
void sample_combat_inputs(std::uint16_t* out);
void enable_roster() {
    if (std::filesystem::is_regular_file(combat_module_path) &&
        std::filesystem::is_regular_file(std::filesystem::u8path(jp_rom_path)))
        selection.available_fighters = 0x017FFFFFu; // All 24 original JP grid choices.
    else
        selection.available_fighters = 0;
}

void return_from_guest_hook(ArmCpuState* cpu) {
    const auto target = cpu->R[14];
    cpu->R[15] = target & ~1u;
    cpu->cpsr = (cpu->cpsr & ~CPSR_T_BIT) | ((target & 1) ? CPSR_T_BIT : 0);
    runtime_call_should_return(cpu->R[15]);
}
// Called only for short, returning cartridge drawing helpers. A private return
// marker balances the generated call ABI; the enclosing callback's CPU survives.
std::uint32_t call_original_game_function(std::uint32_t pc,
                                          std::initializer_list<std::uint32_t> args = {}) {
    const ArmCpuState saved = g_cpu;
    unsigned reg = 0;
    for (auto arg : args)
        g_cpu.R[reg++] = arg;
    constexpr std::uint32_t stop = gba_address::kHookReturnMarker;
    g_cpu.R[14] = stop | 1;
    g_cpu.R[15] = pc;
    g_cpu.cpsr |= CPSR_T_BIT;
    runtime_call_push_return(stop);
    runtime_dispatch_with_exchange(pc | 1);
    const auto result = g_cpu.R[0];
    runtime_call_cancel_return(stop);
    g_cpu = saved;
    return result;
}
void write_input(unsigned player) {
    const auto& s = inputs.players[player];
    const auto& a = kUsInputAddresses[player];
    bus_write_u32(a.held, s.held);
    bus_write_u32(a.previous, s.previous);
    bus_write_u32(a.pressed, s.pressed);
    bus_write_u32(a.released, s.released);
    bus_write_u32(a.repeat, s.repeat);
    bus_write_u32(a.recent_repress, s.recent_repress);
    for (unsigned i = 0; i < 10; ++i)
        bus_write_u8(a.repeat_ticks + i, s.repeat_ticks[i]);
    for (unsigned i = 0; i < 16; ++i)
        bus_write_u16(a.history + 2 * i, s.history[i]);
}
CustomPalette selected_ms_palette(unsigned seat, std::uint8_t fighter, std::uint8_t color) {
    if (!valid_jp_color(fighter, color))
        return {};
    const auto choice = jp_combat_choice(fighter, color);
    return ms_colors.get(battle_mode == MenuBattleMode::Online ? 0 : seat, choice.fighter,
                         choice.palette);
}
void battle_entry(MenuBattleMode mode) {
    battle_mode = mode;
    selector_active = true;
    inputs = {};
    selection = {};
    selection.training = mode == MenuBattleMode::Training;
    selection.jp_layout = true;
    selection.palette_for = selected_ms_palette;
    selection.seats[0].cursor = 13;
    selection.seats[1].cursor = 22;
    enable_roster();
    inputs.advance(sampled_player_inputs[0],
                   sampled_player_inputs[1]); // Do not carry the menu's confirm edge.
    bus_write_u8(us_address::kGameMode,
                 us_address::kModePresetVersus); // Reuse Preset VS battle rules, without serial.
    bus_write_u8(us_address::kLinkStatus, 0);
    bus_write_u8(us_address::kSelectedFighter1, selection.seats[0].fighter + 1);
    bus_write_u8(us_address::kSelectedFighter2, selection.seats[1].fighter + 1);
    bus_write_u32(us_address::kNextScene, 7);
    bus_write_u32(us_address::kGameState, us_address::kStateCharacterSelect);
    bus_write_u32(us_address::kSubstate, 0);
    if (mode == MenuBattleMode::Online) {
        online_room.close();
        last_online_result = {};
        online_qualification_frames = online_qualification_end_frame = 0;
        online_lobby.begin();
        show_online_lobby(&online_lobby);
    }
    std::printf("jp_entry: %s -> JP character select\n",
                mode == MenuBattleMode::Online     ? "online lobby"
                : mode == MenuBattleMode::Training ? "training"
                                                   : "local VS");
}
int update_us_menu_and_inputs(std::uint32_t, int, ArmCpuState* cpu) {
    auto state = bus_read_u32(us_address::kGameState);
    set_invite_busy(!selector_active && state > 4);
    if (state == us_address::kStateMainMenu)
        rename_local_vs_menu_item();
    if (!pending_invite.empty()) {
        if (selector_active && state == us_address::kStateCharacterSelect) {
            leave_select();
            return_from_guest_hook(cpu);
            return 1;
        }
        if (!selector_active && state == us_address::kStateMainMenu &&
            bus_read_u8(us_address::kMenuReadyPhase) == 3) {
            battle_entry(MenuBattleMode::Online);
            std::snprintf(online_lobby.invite.data(), online_lobby.invite.size(), "%s",
                          pending_invite.c_str());
            online_lobby.action = OnlineLobbyAction::Join;
            pending_invite.clear();
            invite_nav_frames = 0;
            state = 11;
        }
    }
    const auto entry = menu_route.observe(state, bus_read_u8(us_address::kMenuCursorX),
                                          bus_read_u8(us_address::kMenuCursorY));
    if (!selector_active && entry != MenuBattleMode::None)
        battle_entry(entry);
    if (!selector_active)
        return 0;
    if (state == us_address::kStateMainMenu || state == 1 || state == 2) {
        selector_active = false;
        battle_mode = MenuBattleMode::None;
        online_room.close();
        show_online_lobby(nullptr);
        return 0;
    }
    for (unsigned player = 0; player < 2; ++player) {
        inputs.players[player].advance(sampled_player_inputs[player]);
        write_input(player);
    }
    return_from_guest_hook(cpu);
    return 1;
}
int initialize_us_selection(std::uint32_t pc, int, ArmCpuState*) {
    if (!selector_active)
        return 0;
    selection = {};
    selection.training = battle_mode == MenuBattleMode::Training;
    selection.jp_layout = true;
    selection.palette_for = selected_ms_palette;
    enable_roster();
    for (unsigned player = 0; player < 2; ++player) {
        const auto id = bus_read_u8(us_address::kSelectedFighter1 + player);
        if (id >= 1 && id <= 25) {
            auto& seat = selection.seats[player];
            seat.fighter = id - 1;
            seat.cursor = static_cast<std::uint8_t>(
                std::find(kJpPortraits.begin(), kJpPortraits.end(), seat.fighter) -
                kJpPortraits.begin());
        }
    }
    gba_mod_set_function_hook_enabled(kSelectBeginHook, 0);
    runtime_dispatch_with_exchange(pc | 1);
    gba_mod_set_function_hook_enabled(kSelectBeginHook, 1);
    return 1;
}
int map_second_player_input(std::uint32_t, int, ArmCpuState* cpu) {
    if (!selector_active || bus_read_u32(us_address::kGameState) != 9 || cpu->R[1] != 1)
        return 0;
    const auto address = cpu->R[2];
    const auto& p = inputs.players[1];
    if (address == us_address::kPlayer2BattleHeld)
        bus_write_u32(address, p.held);
    else if (address == us_address::kPlayer2BattlePressed)
        bus_write_u32(address, p.pressed);
    else if (address == us_address::kPlayer2BattleRepress)
        bus_write_u32(address, p.recent_repress);
    else
        return 0;
    // Run the stock button-remapping function once with the local raw input.
    // Its caller's real LR and call frame are retained, including IRQ state.
    gba_mod_set_function_hook_enabled(kMapHook, 0);
    runtime_dispatch_with_exchange(us_address::kMapFighterButtons | 1);
    gba_mod_set_function_hook_enabled(kMapHook, 1);
    return 1;
}
void leave_select() {
    selector_active = false;
    battle_mode = MenuBattleMode::None;
    online_room.close();
    show_online_lobby(nullptr);
    bus_write_u32(us_address::kNextScene, 4);
    bus_write_u32(us_address::kSubstate, 10); // Original fade-out and hardware cleanup.
}
int update_jp_selector(std::uint32_t, int, ArmCpuState* cpu) {
    if (!selector_active)
        return 0;
    std::uint8_t changed = 0;
    if (battle_mode == MenuBattleMode::Online) {
        if (online_lobby.cancelled) {
            leave_select();
            return_from_guest_hook(cpu);
            return 1;
        }
        if (online_lobby.action != OnlineLobbyAction::None) {
            const auto action = online_lobby.action;
            online_lobby.action = OnlineLobbyAction::None;
            try {
                if (action == OnlineLobbyAction::Host)
                    online_room.host(std::uint16_t(online_lobby.port), online_lobby.address.data(),
                                     {}, online_lobby.player_name.data());
                else
                    online_room.join(online_lobby.invite.data(), {},
                                     online_lobby.player_name.data());
                online_lobby.hosting = true;
                online_lobby.invitation = online_room.invitation();
            } catch (const std::exception& error) {
                online_lobby.hosting = false;
                online_lobby.status = error.what();
            }
        }
        if (online_room.active()) {
            online_room.tick(selection, online_lobby.open ? 0 : sampled_player_inputs[0]);
            online_lobby.code = online_room.code();
            online_lobby.invitation = online_room.invitation();
            online_lobby.status = online_room.status();
            if (online_room.failed()) {
                selection.cancelled = false;
                for (auto& seat : selection.seats)
                    seat.ready = false;
                online_lobby.begin();
                online_lobby.status = online_room.status();
                show_online_lobby(&online_lobby);
            }
            if (online_room.connected() && online_lobby.open) {
                online_lobby.open = false;
                show_online_lobby(nullptr);
                inputs = {};
            }
            if (online_room.launching())
                selection.launch = true;
        }
        if (!selection.launch) {
            if (selection.cancelled)
                leave_select();
            return_from_guest_hook(cpu);
            return 1;
        }
    } else
        changed = selection.advance(inputs);
    if (selection.cancelled) {
        leave_select();
        return_from_guest_hook(cpu);
        return 1;
    }
    // The JP compositor owns the visible selection. Keep its larger cursor
    // indices and donor-only fighters away from the stock US sprite tables.
    if (changed && !bus_read_u8(us_address::kSelectionSoundGate))
        call_original_game_function(us_address::kPlaySelectionSound, {14});
    if (selection.launch) {
        if (!selection.supported(selection.seats[0].fighter) ||
            !selection.supported(selection.seats[1].fighter)) {
            return_from_guest_hook(cpu);
            return 1;
        }
        static HMODULE module = LoadLibraryW(combat_module_path.c_str());
        const auto run =
            module ? reinterpret_cast<RunJpCombat>(GetProcAddress(module, "SeedRunJpCombat"))
                   : nullptr;
        int result = -1;
        if (run) {
            JpCombatRequest request;
            request.surface = suspend_menu_host();
            // Automated menu input must not strand the interactive JP module
            // on a held/neutral script when the player takes over its window.
            // Headless oracles keep their explicit scripted input streams.
            if (request.surface.window) {
                scripted_input_enabled.store(false);
                sampled_player_inputs = {};
                request.surface.inputs->release_guard();
            }
            request.surface.online = battle_mode == MenuBattleMode::Online;
            OnlineCombatResult online_result;
            if (request.surface.online) {
                request.connection = online_room.connection();
                request.online_result = &online_result;
                for (unsigned p = 0; p < 2; ++p)
                    std::snprintf(request.player_names[p], sizeof(request.player_names[p]), "%s",
                                  online_room.names()[p].c_str());
                request.qualification_frames = online_qualification_frames;
                request.qualification_end_frame = online_qualification_end_frame;
            }
            request.mode = battle_mode == MenuBattleMode::Training ? JpCombatMode::Training
                                                                   : JpCombatMode::Versus;
            request.rom = jp_rom_path.c_str();
            request.bios = bios_path.c_str();
            request.save = jp_save_path.c_str();
            request.debug_port = jp_debug_port;
            request.sample_inputs = sample_combat_inputs;
            for (unsigned p = 0; p < 2; ++p) {
                const auto choice =
                    jp_combat_choice(selection.seats[p].fighter, selection.seats[p].color);
                request.fighters[p] = choice.fighter;
                request.colors[p] = choice.palette;
                request.palettes[p] = selection.seats[p].palette;
            }
            set_invite_busy(true);
            result = run(&request);
            set_invite_busy(false);
            resume_menu_host();
            if (request.surface.online) {
                last_online_result = online_result;
                if (result >= 0 && online_result.converged) {
                    online_room.match_returned(selection, online_result.final_crc);
                    online_lobby.open = false;
                    online_lobby.status = online_room.status();
                    show_online_lobby(nullptr);
                } else {
                    online_room.close();
                    online_lobby.begin();
                    online_lobby.status = online_result.message;
                    if (online_lobby.status.empty())
                        online_lobby.status = "Online match ended. Connect again to play.";
                    show_online_lobby(&online_lobby);
                }
            }
            if (result == 2) {
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            }
        }
        if (!run) {
            std::fprintf(stderr, "JP combat module load failed: %lu\n", GetLastError());
            if (battle_mode == MenuBattleMode::Online) {
                online_room.close();
                online_lobby.begin();
                online_lobby.status =
                    "Could not load the JP combat module. Extract the entire portable ZIP again.";
                show_online_lobby(&online_lobby);
            }
        }
        std::fprintf(stderr, "local_vs: JP combat returned %d\n", result);
        for (auto& seat : selection.seats)
            seat.ready = false;
        selection.ready_frames = 0;
        selection.launch = false;
        inputs = {};
        inputs.advance(sampled_player_inputs[0], sampled_player_inputs[1]);
        for (unsigned p = 0; p < 2; ++p)
            write_input(p);
    }
    return_from_guest_hook(cpu);
    return 1;
}
std::uint16_t sample_player_inputs(const gbarecomp::TouchFrameInfo*) {
    std::string incoming;
    if (take_game_invite(incoming)) {
        pending_invite = std::move(incoming);
        invite_nav_frames = 0;
    }
    if (!pending_invite.empty() && !selector_active) {
        const auto held =
            bus_read_u32(us_address::kGameState) <= 3 && (++invite_nav_frames % 60) < 3 ? 8u : 0u;
        sampled_player_inputs = {std::uint16_t(held), 0};
        return std::uint16_t(1023 ^ held);
    }
    if (scripted_input_enabled.load()) {
        const auto packed = scripted_player_inputs.load();
        sampled_player_inputs = {std::uint16_t(packed & 1023),
                                 std::uint16_t((packed >> 16) & 1023)};
        return 0x3FF ^ sampled_player_inputs[0];
    }
    sampled_player_inputs = {};
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return 0x3FF;
    sample_host_players(sampled_player_inputs.data());
    return 0x3FF;
}
void sample_combat_inputs(std::uint16_t* out) {
    sample_player_inputs(nullptr);
    out[0] = sampled_player_inputs[0];
    out[1] = sampled_player_inputs[1];
}
// Available only through the runtime's explicitly enabled debug TCP server.
// Scripts use the same input coordinator and cartridge path as physical pads.
int handle_debug_command(const char* request, void (*write)(void*, const char*, std::size_t),
                         void* context) {
    gbarecomp::json::Value value;
    if (!gbarecomp::json::parse(request, value))
        return 0;
    const auto cmd = value.str("cmd");
    if (cmd == "seed_online_host" || cmd == "seed_online_join" ||
        cmd == "seed_online_qualification") {
        if (!selector_active || battle_mode != MenuBattleMode::Online)
            return 0;
        if (cmd == "seed_online_host") {
            const auto address = value.str("address");
            std::snprintf(online_lobby.address.data(), online_lobby.address.size(), "%s",
                          address.c_str());
            online_lobby.port = std::clamp<int>(value.integer("port", 7000), 1, 65535);
            online_lobby.action = OnlineLobbyAction::Host;
        } else if (cmd == "seed_online_join") {
            const auto invite = value.str("invite");
            std::snprintf(online_lobby.invite.data(), online_lobby.invite.size(), "%s",
                          invite.c_str());
            online_lobby.action = OnlineLobbyAction::Join;
        } else {
            online_qualification_end_frame =
                std::clamp<int>(value.integer("end_frame", 0), 0, 18000);
            online_qualification_frames =
                online_qualification_end_frame
                    ? 0
                    : std::clamp<int>(value.integer("frames", 240), 3, 18000);
        }
        if (!value.str("name").empty())
            std::snprintf(online_lobby.player_name.data(), online_lobby.player_name.size(), "%s",
                          normalize_player_name(value.str("name")).c_str());
        constexpr char reply[] = "{\"ok\":true}";
        write(context, reply, sizeof(reply) - 1);
        return 1;
    }
    if (cmd == "seed_online_cancel") {
        online_lobby.cancel();
        constexpr char reply[] = "{\"ok\":true}";
        write(context, reply, sizeof(reply) - 1);
        return 1;
    }
    if (cmd == "seed_input") {
        scripted_player_inputs.store(std::uint32_t(value.integer("p1", 0) & 1023) |
                                     (std::uint32_t(value.integer("p2", 0) & 1023) << 16));
        scripted_input_enabled.store(value.flag("enabled", true));
        constexpr char reply[] = "{\"ok\":true}";
        write(context, reply, sizeof(reply) - 1);
        return 1;
    }
    if (cmd == "seed_selector_pixels") {
        const auto& pixels = jp_art.compose(selection);
        static constexpr char hex[] = "0123456789abcdef";
        std::string reply = "{\"ok\":true,\"w\":240,\"h\":160,\"data\":\"";
        reply.reserve(reply.size() + pixels.size() * 6 + 2);
        for (const auto p : pixels)
            for (unsigned shift : {0u, 5u, 10u}) {
                const auto c = ((p >> shift) & 31) * 255 / 31;
                reply += hex[c >> 4];
                reply += hex[c & 15];
            }
        reply += "\"}";
        write(context, reply.data(), reply.size());
        return 1;
    }
    if (cmd != "seed_status")
        return 0;
    char reply[4096];
    const int size = std::snprintf(
        reply, sizeof(reply),
        "{\"ok\":true,\"selector\":\"jp-destiny\",\"scripted_input\":%s,\"battle_mode\":\"%s\","
        "\"online_lobby\":%s,\"network_ready\":%s,\"active\":%s,\"state\":%u,\"substate\":%u,"
        "\"control\":%u,\"mode\":%u,\"link\":%u,"
        "\"cursor\":[%u,%u],\"fighters\":[%u,%u],\"preview\":[%u,%u],\"ready\":[%s,%s],\"raw\":[%u,"
        "%u],\"battle_input\":[%u,%u],\"hp\":[%u,%u],\"depth\":%u,"
        "\"online_invitation\":\"%s\",\"online_frames\":%u,\"online_crc\":%u,\"online_peer_crc\":%"
        "u,\"online_converged\":%s,\"online_rollbacks\":%u,\"online_desyncs\":%u}",
        scripted_input_enabled.load() ? "true" : "false",
        battle_mode == MenuBattleMode::Online     ? "online"
        : battle_mode == MenuBattleMode::Training ? "training"
        : battle_mode == MenuBattleMode::Local    ? "local"
                                                  : "none",
        online_lobby.open ? "true" : "false", online_room.connected() ? "true" : "false",
        selector_active ? "true" : "false", bus_read_u32(us_address::kGameState),
        bus_read_u32(us_address::kSubstate), bus_read_u8(us_address::kSelectionControlPhase),
        bus_read_u8(us_address::kGameMode), bus_read_u8(us_address::kLinkStatus),
        selection.seats[0].cursor, selection.seats[1].cursor,
        bus_read_u8(us_address::kSelectedFighter1), bus_read_u8(us_address::kSelectedFighter2),
        selection.seats[0].fighter, selection.seats[1].fighter,
        selection.seats[0].ready ? "true" : "false", selection.seats[1].ready ? "true" : "false",
        inputs.players[0].held, inputs.players[1].held,
        bus_read_u32(us_address::kPlayer1BattleHeld), bus_read_u32(us_address::kPlayer2BattleHeld),
        bus_read_u16(us_address::kPlayer1Health), bus_read_u16(us_address::kPlayer2Health),
        runtime_call_stack_depth(), online_room.invitation().c_str(), last_online_result.frames,
        last_online_result.final_crc, last_online_result.peer_crc,
        last_online_result.converged ? "true" : "false", last_online_result.rollbacks,
        last_online_result.desyncs);
    std::string extended(reply,
                         static_cast<std::size_t>(std::clamp(size - 1, 0, int(sizeof(reply) - 2))));
    extended += ",\"colors\":[" + std::to_string(selection.seats[0].color) + "," +
                std::to_string(selection.seats[1].color) + "]";
    extended += ",\"custom_colors\":[" +
                std::string(selection.seats[0].palette.enabled ? "true" : "false") + "," +
                (selection.seats[1].palette.enabled ? "true" : "false") + "]";
    extended += ",\"online_matches\":" + std::to_string(online_room.completed_matches()) +
                ",\"online_epoch\":" + std::to_string(online_room.epoch());
    extended += ",\"online_status\":";
    gbarecomp::json::append_quoted(extended, online_lobby.status);
    extended += ",\"players\":[";
    gbarecomp::json::append_quoted(extended, online_room.names()[0]);
    extended += ",";
    gbarecomp::json::append_quoted(extended, online_room.names()[1]);
    extended += "]}";
    write(context, extended.data(), extended.size());
    return 1;
}
void overlay(gbarecomp::HostOverlay* out) {
    using namespace gbarecomp;
    const auto state = bus_read_u32(us_address::kGameState);
    if (selector_active && state == us_address::kStateCharacterSelect) {
        const auto& pixels = jp_art.compose(selection);
        draw_jp_selection(out, pixels);
    }
}
} // namespace
void configure_local_vs(gbarecomp::RunOptions& options, const std::filesystem::path& art_path,
                        const std::string& jp_rom, const std::string& bios, const std::string& save,
                        int debug_port) {
    jp_art.load(art_path);
    std::string palette_error;
    if (!ms_colors.load(art_path.parent_path().parent_path() / "ms-colors.ini", palette_error))
        std::fprintf(stderr, "%s\n", palette_error.c_str());
    jp_rom_path = jp_rom;
    bios_path = bios;
    jp_save_path = save;
    jp_debug_port = debug_port;
    combat_module_path = art_path.parent_path().parent_path() / "SeedJpCombat.dll";
    for (const auto& hook :
         std::array<std::pair<const char*, std::pair<std::uint32_t, GBAModFunctionEntryCallback>>,
                    4>{
             {{kInputHook, {us_address::kPlayer1InputUpdate, update_us_menu_and_inputs}},
              {kSelectHook, {us_address::kCharacterSelectTick, update_jp_selector}},
              {kMapHook, {us_address::kMapFighterButtons, map_second_player_input}},
              {kSelectBeginHook, {us_address::kCharacterSelectBegin, initialize_us_selection}}}}) {
        gba_mod_register_function_entry_plugin(hook.first, hook.second.first, 1,
                                               hook.second.second);
        gba_mod_set_function_hook_enabled(hook.first, 1);
    }
    options.input_frame = sample_player_inputs;
    options.host_overlay = overlay;
    options.tcp_command = handle_debug_command;
}
} // namespace seed
