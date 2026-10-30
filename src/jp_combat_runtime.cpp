#include "jp_combat_runtime.h"
#include "move_list.h"
#include "game_addresses.h"
#include "jp_frame_probe.h"
#include "fighter_outline.h"
#include "fighter_outline_setting.h"
#include "training_tools.h"
#include "mini_json.h"
#include "mod_function_hooks.h"
#include "mod_state.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

namespace gba_address = seed::address::common;
namespace jp_address = seed::address::jp;

namespace seed {
namespace {
JpCombatRequest match_request;
unsigned startup_frames = 0;
bool match_started = false, match_ended = false;
bool rollback_frame_driven = false;
std::atomic<bool> scripted_input_enabled{false};
std::atomic<std::uint32_t> scripted_player_inputs{0};
std::array<std::uint16_t, 2> sampled_player_inputs{};
JpFrameProbe frame_probe;
bool meter_frame_pending = false, meter_global_hitstop = false;
struct MeterBus {
    std::uint8_t u8(std::uint32_t address) const {
        return bus_read_u8(address);
    }
    std::uint16_t u16(std::uint32_t address) const {
        return bus_read_u16(address);
    }
    std::uint32_t u32(std::uint32_t address) const {
        return bus_read_u32(address);
    }
};
int begin_meter_frame(std::uint32_t, int, ArmCpuState*) {
    meter_frame_pending = match_started && training_frame_meter().allowed();
    meter_global_hitstop =
        meter_frame_pending &&
        (bus_read_u8(jp_address::kBattleContext + jp_address::kBattleSlowdownOffset) & 15);
    return 0; // Observational; the native function always executes unchanged.
}
int finish_meter_frame(std::uint32_t, int, ArmCpuState*) {
    if (meter_frame_pending && training_frame_meter().allowed() &&
        bus_read_u32(jp_address::kGameState) == jp_address::kStateTraining) {
        meter_frame_pending = false;
        training_frame_meter().observe(frame_probe.sample(MeterBus{}, meter_global_hitstop));
    }
    return 0;
}
void return_from_guest_hook(ArmCpuState* cpu) {
    const auto target = cpu->R[14];
    cpu->R[15] = target & ~1u;
    cpu->cpsr = (cpu->cpsr & ~CPSR_T_BIT) | ((target & 1) ? CPSR_T_BIT : 0);
    runtime_call_should_return(cpu->R[15]);
}
void call_original_game_function(std::uint32_t pc, std::initializer_list<std::uint32_t> args) {
    const auto saved = g_cpu;
    unsigned reg = 0;
    for (auto arg : args)
        g_cpu.R[reg++] = arg;
    constexpr std::uint32_t stop = gba_address::kHookReturnMarker;
    g_cpu.R[14] = stop | 1;
    g_cpu.R[15] = pc;
    g_cpu.cpsr |= CPSR_T_BIT;
    runtime_call_push_return(stop);
    runtime_dispatch_with_exchange(pc | 1);
    runtime_call_cancel_return(stop);
    g_cpu = saved;
}
void start_match() {
    // Standard, non-link combat. Both fighters use their original Destiny
    // behavior and asset tables; P2's control selector is replaced below.
    const bool training = match_request.mode == JpCombatMode::Training;
    // The complete snapshot codec requires an emulated clock even for a
    // cartridge without RTC hardware. Pin Training to the same epoch used by
    // rollback; the game never observes a host clock through this inactive chip.
    if (training)
        if (auto* bus = gbarecomp::active_bus())
            bus->rtc().set_emulated_clock(946684800);
    bus_write_u32(jp_address::kGameState,
                  training ? jp_address::kStateTraining : jp_address::kStateVersus);
    bus_write_u32(jp_address::kSubstate, 0);
    bus_write_u32(jp_address::kStartupResetField, 0);
    bus_write_u8(jp_address::kSceneSetupByte, 1);
    bus_write_u8(jp_address::kMatchRuleMode, training ? 2 : 6);
    for (unsigned p = 0; p < 2; ++p) {
        bus_write_u8(jp_address::kSelectedFighter1 + p, match_request.fighters[p]);
        bus_write_u8(jp_address::kPlayerPaletteFlags + p, match_request.colors[p]);
        bus_write_u8(jp_address::kPlayerStartupParameterA + p, 4);
        bus_write_u8(jp_address::kPlayerStartupParameterB + p, 0);
        for (unsigned stat = 0; stat < 3; ++stat)
            bus_write_u8(jp_address::kPlayerStatParameters +
                             p * jp_address::kPlayerParameterStride + stat,
                         100);
        bus_write_u8(jp_address::kPlayerButtonMapping + p,
                     0xE1); // Identity mapping for raw A/B/L/R.
    }
    bus_write_u8(jp_address::kSelectedStage, match_request.stage);
    bus_write_u8(jp_address::kSharedStartupParameterB, 0);
    match_started = true;
    sampled_player_inputs = {};
    combat_presentation_enabled(true);
    std::printf("jp_combat: native fighters %u / %u, %s, no serial\n", match_request.fighters[0],
                match_request.fighters[1], training ? "training" : "versus");
}
// Respect the player's explicit choice instead of the cartridge's forced P2
// recolor when both seats choose the same fighter and palette. Use the current
// fighter argument for palette resources so native form changes still work.
int selected_palette_flag(std::uint32_t, int, ArmCpuState* cpu) {
    if (!match_started || cpu->R[0] > 1)
        return 0;
    cpu->R[0] = match_request.colors[cpu->R[0]];
    return_from_guest_hook(cpu);
    return 1;
}
int selected_palette_pointer(std::uint32_t, int, ArmCpuState* cpu) {
    const auto seat = cpu->R[0];
    if (!match_started || seat > 1)
        return 0;
    const auto fighter = cpu->R[1 + seat];
    if (fighter > 27)
        return 0;
    if (match_request.palettes[seat].enabled && fighter == match_request.fighters[seat]) {
        const auto buffer = jp_address::kFighterPaletteBuffers + seat * kMsPaletteBytes;
        for (unsigned i = 0; i < kMsPaletteColors; ++i)
            bus_write_u16(buffer + i * 2, match_request.palettes[seat].colors[i]);
        cpu->R[0] = buffer;
        return_from_guest_hook(cpu);
        return 1;
    }
    cpu->R[0] = bus_read_u32(jp_address::kFighterDescriptorTable +
                             jp_address::kFighterDescriptorStride * fighter +
                             (match_request.colors[seat] ? 12 : 8));
    return_from_guest_hook(cpu);
    return 1;
}
int update_jp_inputs(std::uint32_t, int, ArmCpuState* cpu) {
    if (!match_started && ++startup_frames >= 120)
        start_match();
    if (match_started &&
        bus_read_u32(jp_address::kGameState) != (match_request.mode == JpCombatMode::Training
                                                     ? jp_address::kStateTraining
                                                     : jp_address::kStateVersus)) {
        match_ended = true;
        if (!rollback_frame_driven)
            return_from_combat();
    }
    if (scripted_input_enabled.load()) {
        const auto packed = scripted_player_inputs.load();
        sampled_player_inputs = {std::uint16_t(packed & 1023),
                                 std::uint16_t((packed >> 16) & 1023)};
    } else {
        sampled_player_inputs = {};
        if (match_request.sample_inputs && match_started)
            match_request.sample_inputs(sampled_player_inputs.data());
    }
    call_original_game_function(jp_address::kUpdateInputState,
                                {jp_address::kPlayer1InputState, sampled_player_inputs[0]});
    call_original_game_function(jp_address::kUpdateInputState,
                                {jp_address::kPlayer2InputState, sampled_player_inputs[1]});
    return_from_guest_hook(cpu);
    return 1;
}
int update_second_fighter_input(std::uint32_t, int, ArmCpuState* cpu) {
    if (!match_started || match_request.mode == JpCombatMode::Training)
        return 0; // Keep the native practice dummy and settings.
    call_original_game_function(
        jp_address::kApplyFighterInput,
        {cpu->R[0] + jp_address::kRightFighterInputOffset, jp_address::kPlayer2InputState});
    return_from_guest_hook(cpu);
    return 1;
}
int handle_debug_command(const char* request, void (*write)(void*, const char*, std::size_t),
                         void* context) {
    gbarecomp::json::Value value;
    if (!gbarecomp::json::parse(request, value))
        return 0;
    const auto cmd = value.str("cmd");
    if (cmd == "seed_input") {
        scripted_player_inputs.store(std::uint32_t(value.integer("p1", 0) & 1023) |
                                     (std::uint32_t(value.integer("p2", 0) & 1023) << 16));
        scripted_input_enabled.store(value.flag("enabled", true));
        constexpr char reply[] = "{\"ok\":true}";
        write(context, reply, sizeof(reply) - 1);
        return 1;
    }
    if (cmd == "seed_training_tools") {
        const auto action = value.str("action");
        if (action == "save" || action == "load")
            training_request_state(action == "load");
        else if (action == "boxes") {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.scancode = SDL_SCANCODE_F6;
            training_tools_event(event, match_request.surface.online);
        }
        const auto view = training_view();
        std::string reply =
            "{\"ok\":true,\"allowed\":" + std::string(training_tools_allowed() ? "true" : "false") +
            ",\"boxes_visible\":" + (training_boxes_visible() ? "true" : "false") +
            ",\"pending\":" + (training_checkpoint_pending() ? "true" : "false") +
            ",\"paused\":" + (view.paused ? "true" : "false") +
            ",\"cursor\":" + std::to_string(view.cursor) + ",\"boxes\":[";
        for (unsigned i = 0; i < view.boxes.size(); ++i) {
            if (i)
                reply += ',';
            const auto& b = view.boxes[i];
            reply += "[" + std::to_string(b.x) + "," + std::to_string(b.y) + "," +
                     std::to_string(b.w) + "," + std::to_string(b.h) + "," +
                     (b.attack ? "true" : "false") + "]";
        }
        reply += "]}";
        write(context, reply.data(), reply.size());
        return 1;
    }
    if (cmd == "seed_frame_meter") {
        auto& meter = training_frame_meter();
        const auto action = value.str("action");
        if (action == "toggle")
            meter.toggle();
        else if (action == "clear") {
            meter.clear();
            frame_probe.reset();
        }
        std::string reply =
            "{\"ok\":true,\"allowed\":" + std::string(meter.allowed() ? "true" : "false") +
            ",\"visible\":" + (meter.visible() ? "true" : "false") +
            ",\"frames\":" + std::to_string(meter.frames()) + ",\"history\":[";
        for (unsigned seat = 0; seat < 2; ++seat) {
            if (seat)
                reply += ',';
            reply += '[';
            for (std::size_t index = 0; index < meter.size(); ++index) {
                if (index)
                    reply += ',';
                reply += std::to_string(static_cast<unsigned>(meter.cell(seat, index).phase));
            }
            reply += ']';
        }
        reply += "],\"summaries\":[";
        for (unsigned seat = 0; seat < 2; ++seat) {
            if (seat)
                reply += ',';
            const auto& summary = meter.summary(seat);
            reply +=
                "{\"startup\":" + (summary.startup ? std::to_string(*summary.startup) : "null") +
                ",\"preactive\":" + std::to_string(summary.preactive) +
                ",\"active\":" + std::to_string(summary.active) +
                ",\"recovery\":" + std::to_string(summary.recovery) +
                ",\"gap\":" + std::to_string(summary.gap) +
                ",\"hitstun\":" + std::to_string(summary.hitstun) +
                ",\"hitstop\":" + std::to_string(summary.hitstop) +
                ",\"interrupted\":" + (summary.interrupted ? "true" : "false") + "}";
        }
        reply += "],\"projectiles\":[";
        for (unsigned seat = 0; seat < 2; ++seat) {
            if (seat)
                reply += ',';
            reply += '[';
            for (std::size_t index = 0; index < meter.size(); ++index) {
                if (index)
                    reply += ',';
                reply += meter.cell(seat, index).projectile ? "true" : "false";
            }
            reply += ']';
        }
        reply += "]}";
        write(context, reply.data(), reply.size());
        return 1;
    }
    if (cmd != "seed_status")
        return 0;
    char reply[1024];
    const int n = std::snprintf(
        reply, sizeof(reply),
        "{\"ok\":true,\"engine\":\"jp\",\"scripted_input\":%s,\"training\":%s,"
        "\"started\":%s,"
        "\"ended\":%s,\"state\":%u,\"substate\":%u,\"round\":%u,\"fighters\":[%u,"
        "%u],\"raw\":[%u,%"
        "u],\"battle_input\":[%u,%u],\"hp\":[%u,%u],\"depth\":%u}",
        scripted_input_enabled.load() ? "true" : "false",
        match_request.mode == JpCombatMode::Training ? "true" : "false",
        match_started ? "true" : "false", match_ended ? "true" : "false",
        bus_read_u32(jp_address::kGameState), bus_read_u32(jp_address::kSubstate),
        bus_read_u8(jp_address::kRoundPhase), bus_read_u8(jp_address::kPlayer1FighterId) & 31,
        bus_read_u8(jp_address::kPlayer2FighterId) & 31, sampled_player_inputs[0],
        sampled_player_inputs[1], bus_read_u16(jp_address::kPlayer1MappedInput),
        bus_read_u16(jp_address::kPlayer2MappedInput), bus_read_u16(jp_address::kPlayer1Health),
        bus_read_u16(jp_address::kPlayer2Health), runtime_call_stack_depth());
    std::string extended(reply, n - 1);
    extended += ",\"colors\":[" + std::to_string(match_request.colors[0]) + "," +
                std::to_string(match_request.colors[1]) + "]}";
    extended.pop_back();
    extended += ",\"stage\":" +
                std::to_string(
                    bus_read_u8(jp_address::kBattleContext + jp_address::kBattleStageOffset) & 31) +
                ",\"requested_stage\":" + std::to_string(match_request.stage) + "}";
    extended.pop_back();
    const auto move_fighters = movelist_fighters();
    extended += ",\"movelist_fighters\":[" + std::to_string(move_fighters[0]) + "," +
                std::to_string(move_fighters[1]) + "],\"movelist_default\":" +
                std::to_string(movelist_default_fighter()) + "}";
    write(context, extended.data(), extended.size());
    return 1;
}
void save_host_state(gbarecomp::debug::SnapshotWriter& writer) {
    writer.u32(startup_frames);
    writer.boolean(match_started);
    writer.boolean(match_ended);
    for (auto held : sampled_player_inputs)
        writer.u16(held);
    for (auto fighter : match_request.fighters)
        writer.u8(fighter);
    for (auto color : match_request.colors)
        writer.u8(color);
    for (const auto& palette : match_request.palettes) {
        writer.boolean(palette.enabled);
        for (auto color : palette.colors)
            writer.u16(color);
    }
    writer.u32(static_cast<unsigned>(match_request.mode));
    writer.u8(match_request.stage);
}
bool preflight_host_state(gbarecomp::debug::SnapshotReader& reader) {
    const auto boot = reader.u32();
    const auto did_start = reader.boolean(), did_end = reader.boolean();
    const auto p1 = reader.u16(), p2 = reader.u16();
    const auto left = reader.u8(), right = reader.u8();
    const bool fighters = left == match_request.fighters[0] && right == match_request.fighters[1];
    const auto color1 = reader.u8(), color2 = reader.u8();
    const bool colors = color1 == match_request.colors[0] && color2 == match_request.colors[1];
    bool custom = true;
    for (const auto& palette : match_request.palettes) {
        custom &= reader.boolean() == palette.enabled;
        for (auto color : palette.colors)
            custom &= reader.u16() == color;
    }
    const auto mode = reader.u32();
    const auto stage = reader.u8();
    return reader.ok() && reader.remaining() == 0 && boot <= 120 && (!did_end || did_start) &&
           (did_start ? boot == 120 : boot < 120) && !(p1 & ~1023u) && !(p2 & ~1023u) && fighters &&
           colors && custom && mode == static_cast<unsigned>(match_request.mode) &&
           stage == match_request.stage;
}
} // namespace
void set_jp_frame_inputs(std::uint16_t p1, std::uint16_t p2) {
    rollback_frame_driven = true;
    scripted_player_inputs.store((p1 & 1023u) | (std::uint32_t(p2 & 1023u) << 16));
    scripted_input_enabled.store(true);
}
bool jp_combat_startup_ready() {
    // Stop before the native round intro. Unlike presentation predicates this
    // runs only during deterministic boot, before the shared initial checksum.
    return match_started && !match_ended &&
           bus_read_u32(jp_address::kGameState) == jp_address::kStateVersus &&
           bus_read_u16(jp_address::kPlayer1Health) && bus_read_u16(jp_address::kPlayer2Health) &&
           (bus_read_u8(jp_address::kBattleContext + jp_address::kBattleStageOffset) & 31) ==
               match_request.stage;
}
bool jp_combat_ended() {
    return match_ended;
}
void configure_jp_combat(gbarecomp::RunOptions& options, const JpCombatRequest& request) {
    if (!valid_jp_stage(request.stage))
        throw std::runtime_error("Invalid JP stage");
    match_request = request;
    set_movelist_fighters({request.fighters[0], request.fighters[1]}, true,
                          request.surface.online ? request.connection.seat : 0);
    const auto* base = SDL_GetBasePath();
    const auto outlines =
        base ? load_fighter_outline_settings(std::filesystem::path(base) / "config/launcher-settings.ini")
             : FighterOutlineSettings{};
    set_fighter_outlines_enabled(outlines.enabled);
    set_fighter_outline_opacity(unsigned(outlines.opacity));
    // The predicate must not read through the guest bus: even a RAM read
    // updates hardware open-bus bookkeeping. These existing lifecycle flags
    // are side-effect-free host reads and already track leaving combat.
    configure_fighter_outline([] { return match_started && !match_ended; });
    training_frame_meter().configure(request.mode == JpCombatMode::Training,
                                     request.surface.online);
    configure_training_tools(request.mode == JpCombatMode::Training, request.surface.online,
                             request.save ? std::filesystem::u8path(request.save)
                                          : std::filesystem::path{});
    if (request.mode == JpCombatMode::Training && !request.surface.online) {
        options.checkpoint_pending = training_checkpoint_pending;
        options.checkpoint_service = training_service_checkpoint;
    }
    frame_probe.reset();
    meter_frame_pending = meter_global_hitstop = false;
    startup_frames = 0;
    match_started = match_ended = rollback_frame_driven = false;
    scripted_input_enabled = false;
    scripted_player_inputs = 0;
    sampled_player_inputs = {};
    static const bool state_registered = [] {
        gbarecomp::debug::ModStateProvider provider;
        provider.id = "seed.jp-match";
        provider.schema = 4;
        provider.save = [](void*, gbarecomp::debug::SnapshotWriter& writer, std::string*) {
            save_host_state(writer);
            return true;
        };
        provider.preflight = [](void*, gbarecomp::debug::SnapshotReader& reader, std::string*) {
            return preflight_host_state(reader);
        };
        provider.restore = [](void*, gbarecomp::debug::SnapshotReader& reader) {
            reset_fighter_outline();
            training_presentation_restored();
            startup_frames = reader.u32();
            match_started = reader.boolean();
            match_ended = reader.boolean();
            for (auto& held : sampled_player_inputs)
                held = reader.u16();
            reader.u8();
            reader.u8();
            reader.u8();
            reader.u8();
            for (unsigned seat = 0; seat < 2; ++seat) {
                reader.boolean();
                for (unsigned i = 0; i < kMsPaletteColors; ++i)
                    reader.u16();
            }
            reader.u32();
            reader.u8();
            if (training_frame_meter().allowed()) {
                training_frame_meter().clear();
                frame_probe.reset();
                meter_frame_pending = meter_global_hitstop = false;
            }
        };
        std::string error;
        if (!gbarecomp::debug::register_mod_state_provider(provider, &error))
            throw std::runtime_error(error);
        return true;
    }();
    options.builtin_game_name =
        request.mode == JpCombatMode::Training ? "Gundam SEED Destiny / Battle Assault - Training"
        : request.surface.online               ? "Gundam SEED Destiny / Battle Assault - Online VS"
                                               : "Gundam SEED Destiny / Battle Assault - Local VS";
    options.builtin_rom_sha1 = "64eea52cd909669a972c091869c5f47865863bc0";
    options.diagnostic_replay_default = false;
    options.tcp_command = handle_debug_command;
    borrow_combat_surface(request.surface);
    combat_presentation_enabled(!request.surface.window);
    gba_mod_register_function_entry_plugin("seed.jp-two-inputs", jp_address::kInputDispatch, 1,
                                           update_jp_inputs);
    gba_mod_set_function_hook_enabled("seed.jp-two-inputs", 1);
    gba_mod_register_function_entry_plugin("seed.jp-local-right", jp_address::kRightControlDispatch,
                                           1, update_second_fighter_input);
    gba_mod_set_function_hook_enabled("seed.jp-local-right", 1);
    gba_mod_register_function_entry_plugin("seed.jp-palette-flag", jp_address::kFighterPaletteFlag,
                                           1, selected_palette_flag);
    gba_mod_set_function_hook_enabled("seed.jp-palette-flag", 1);
    gba_mod_register_function_entry_plugin(
        "seed.jp-palette-pointer", jp_address::kFighterPaletteLookup, 1, selected_palette_pointer);
    gba_mod_set_function_hook_enabled("seed.jp-palette-pointer", 1);
    gba_mod_register_function_entry_plugin("seed.jp-meter-begin", jp_address::kBattleFrameBegin, 1,
                                           begin_meter_frame);
    gba_mod_register_function_entry_plugin("seed.jp-meter-tail", jp_address::kBattleFrameTail, 1,
                                           finish_meter_frame);
    gba_mod_set_function_hook_enabled("seed.jp-meter-begin", training_frame_meter().allowed());
    gba_mod_set_function_hook_enabled("seed.jp-meter-tail", training_frame_meter().allowed());
}
} // namespace seed
