#pragma once
#include <array>
#include <filesystem>
#include <string>
namespace seed {
inline constexpr unsigned kPlayerNameBytes = 48;
std::string normalize_player_name(const std::string& name);
bool valid_player_name(const std::string& name);
std::string load_player_name(const std::filesystem::path& folder);
bool save_player_name(const std::filesystem::path& folder, const std::string& name,
                      std::string& error);
std::string online_match_title(const std::array<std::string, 2>& players, unsigned delay,
                               unsigned rollback, float ping);
} // namespace seed
