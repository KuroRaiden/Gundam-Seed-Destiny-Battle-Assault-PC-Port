#include "game_addresses.h"
#include "jp_combat_runtime.h"
#include "runtime_arm.h"
#include "mod_function_hooks.h"
#include "mini_json.h"
#include "mod_state.h"
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
    bus_write_u32(jp_address::kGameState,
                  training ? jp_address::kStateTraining : jp_address::kStateVersus);
    bus_write_u32(jp_address::kSubstate, 0);
    bus_write_u32(jp_address::kStartupResetField, 0);
    bus_write_u8(jp_address::kSceneSetupByte, 1);
    bus_write_u8(jp_address::kMatchRuleMode, training ? 2 : 6);
    for (unsigned p = 0; p < 2; ++p) {
        bus_write_u8(jp_address::kSelectedFighter1 + p, match_request.fighters[p]);
        bus_write_u8(jp_address::kPlayerStartupParameterA + p, 4);
        bus_write_u8(jp_address::kPlayerStartupParameterB + p, 0);
        for (unsigned stat = 0; stat < 3; ++stat)
            bus_write_u8(jp_address::kPlayerStatParameters +
                             p * jp_address::kPlayerParameterStride + stat,
                         100);
        bus_write_u8(jp_address::kPlayerButtonMapping + p,
                     0xE1); // Identity mapping for raw A/B/L/R.
    }
    bus_write_u8(jp_address::kSharedStartupParameterA, 0);
    bus_write_u8(jp_address::kSharedStartupParameterB, 0);
    match_started = true;
    sampled_player_inputs = {};
    combat_presentation_enabled(true);
    std::printf("jp_combat: native fighters %u / %u, %s, no serial\n", match_request.fighters[0],
                match_request.fighters[1], training ? "training" : "versus");
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
    if (cmd != "seed_status")
        return 0;
    char reply[1024];
    const int n = std::snprintf(
        reply, sizeof(reply),
        "{\"ok\":true,\"engine\":\"jp\",\"scripted_input\":%s,\"training\":%s,\"started\":%s,"
        "\"ended\":%s,\"state\":%u,\"substate\":%u,\"round\":%u,\"fighters\":[%u,%u],\"raw\":[%u,%"
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
    write(context, reply, n);
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
    writer.u32(static_cast<unsigned>(match_request.mode));
}
bool preflight_host_state(gbarecomp::debug::SnapshotReader& reader) {
    const auto boot = reader.u32();
    const auto did_start = reader.boolean(), did_end = reader.boolean();
    const auto p1 = reader.u16(), p2 = reader.u16();
    const auto left = reader.u8(), right = reader.u8();
    const bool fighters = left == match_request.fighters[0] && right == match_request.fighters[1];
    const auto mode = reader.u32();
    return reader.ok() && reader.remaining() == 0 && boot <= 120 && (!did_end || did_start) &&
           (did_start ? boot == 120 : boot < 120) && !(p1 & ~1023u) && !(p2 & ~1023u) && fighters &&
           mode == static_cast<unsigned>(match_request.mode);
}
} // namespace
void set_jp_frame_inputs(std::uint16_t p1, std::uint16_t p2) {
    rollback_frame_driven = true;
    scripted_player_inputs.store((p1 & 1023u) | (std::uint32_t(p2 & 1023u) << 16));
    scripted_input_enabled.store(true);
}
bool jp_combat_ended() {
    return match_ended;
}
void configure_jp_combat(gbarecomp::RunOptions& options, const JpCombatRequest& request) {
    match_request = request;
    startup_frames = 0;
    match_started = match_ended = rollback_frame_driven = false;
    scripted_input_enabled = false;
    scripted_player_inputs = 0;
    sampled_player_inputs = {};
    static const bool state_registered = [] {
        gbarecomp::debug::ModStateProvider provider;
        provider.id = "seed.jp-match";
        provider.schema = 1;
        provider.save = [](void*, gbarecomp::debug::SnapshotWriter& writer, std::string*) {
            save_host_state(writer);
            return true;
        };
        provider.preflight = [](void*, gbarecomp::debug::SnapshotReader& reader, std::string*) {
            return preflight_host_state(reader);
        };
        provider.restore = [](void*, gbarecomp::debug::SnapshotReader& reader) {
            startup_frames = reader.u32();
            match_started = reader.boolean();
            match_ended = reader.boolean();
            for (auto& held : sampled_player_inputs)
                held = reader.u16();
            reader.u8();
            reader.u8();
            reader.u32();
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
}
} // namespace seed
