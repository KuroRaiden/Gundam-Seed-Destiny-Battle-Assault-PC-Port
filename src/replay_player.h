#pragma once
#include <filesystem>
#include <string>
namespace seed {
int watch_match_replay(const std::filesystem::path& file, const std::string& rom,
                       const std::string& bios, int scale);
}
