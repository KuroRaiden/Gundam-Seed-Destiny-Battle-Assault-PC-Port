#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace seed {
inline constexpr std::array<std::uint8_t, 3> kTrainingSnapshotBlue{16, 28, 43};
void configure_training_snapshots(bool allowed);
void attach_training_snapshot_observer();
bool request_training_snapshot();
void reset_training_snapshot();
std::vector<std::uint8_t> take_training_snapshot();
std::filesystem::path training_snapshot_filename(const std::filesystem::path& folder);
} // namespace seed
