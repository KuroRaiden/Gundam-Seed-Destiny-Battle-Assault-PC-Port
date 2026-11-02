#pragma once
#include <filesystem>
#include <string>
namespace seed {
struct ReplayLaunchOptions {
    int takeover_player = -1;
    unsigned takeover_frame = 0, export_height = 720;
    bool boxes = false;
    std::string mp4, status, cancel;
};
int watch_match_replay(const std::filesystem::path& file, const std::string& rom,
                       const std::string& bios, int scale, const ReplayLaunchOptions& = {});
} // namespace seed
