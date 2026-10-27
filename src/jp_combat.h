#pragma once
#include "combat_host.h"
#include "net_connection.h"
#include "ms_palette.h"
#include <cstdint>

namespace seed {
inline constexpr unsigned kCombatApiVersion = 7;
struct OnlineCombatResult {
    unsigned frames = 0, rollbacks = 0, replayed_frames = 0, max_rollback = 0, stalls = 0,
             desyncs = 0;
    std::uint32_t initial_crc = 0, final_crc = 0, peer_crc = 0;
    float ping_ms = 0, jitter_ms = 0;
    std::uint16_t hp[2]{};
    bool converged = false;
    char message[192]{};
};
enum class JpCombatMode : unsigned { Versus, Training };
struct JpCombatRequest {
    unsigned version = kCombatApiVersion;
    CombatSurface surface;
    const char* rom = nullptr;
    const char* bios = nullptr;
    const char* save = nullptr;
    int debug_port = 0; // Explicit developer opt-in; no listener in normal play.
    JpCombatMode mode = JpCombatMode::Versus;
    std::uint8_t fighters[2]{1, 8}; // Native JP IDs, including hidden variants 24/26/27.
    std::uint8_t colors[2]{};       // Explicit native palette (0/1), also in mirror matches.
    CustomPalette palettes[2];
    void (*sample_inputs)(std::uint16_t* players) = nullptr;
    char player_names[2][49]{"Player 1", "Player 2"};
    NetConnection connection;
    OnlineCombatResult* online_result = nullptr;
    unsigned qualification_frames = 0;    // Developer oracle only; zero in gameplay.
    unsigned qualification_end_frame = 0; // Exercise negotiated finish boundary.
};
// 0: finished/returned to selection, 2: window closed, negative: startup failure.
using RunJpCombat = int (*)(const JpCombatRequest*);
} // namespace seed
