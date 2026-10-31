#pragma once
#include "ms_palette.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace seed {
inline constexpr unsigned kMaxReplayFrames = 1'000'000;
const char* replay_build_version();
struct MatchReplay {
    std::uint64_t recorded_at = 0;
    bool online = false, complete = false;
    std::array<std::uint8_t, 2> fighters{1, 8}, colors{};
    std::array<CustomPalette, 2> palettes;
    std::uint8_t stage = 0;
    std::array<std::string, 2> names{"Player 1", "Player 2"};
    std::string combat_hash, bios_hash;
    std::string build_version = replay_build_version();
    std::uint32_t initial_crc = 0, final_crc = 0;
    std::array<std::uint64_t, 5> initial_timing{};
    std::vector<std::array<std::uint16_t, 2>> inputs;
};
// A bounded, checksummed input-only format. No machine snapshots or file paths.
MatchReplay read_match_replay(const std::filesystem::path& file);
std::filesystem::path save_match_replay(const std::filesystem::path& folder,
                                        const MatchReplay& replay);
std::string replay_file_hash(const std::filesystem::path& file);
std::string replay_unique_id();
std::filesystem::path combat_library_path();
} // namespace seed
