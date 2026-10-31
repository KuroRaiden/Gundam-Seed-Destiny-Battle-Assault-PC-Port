#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace seed::launcher {
struct TitleLogo {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
// Decode the original JP title's transparent BG1 into host-only memory.
// Original game inputs and this derived image are never bundled with the port.
TitleLogo load_title_logo(const std::filesystem::path& japanese_rom);
} // namespace seed::launcher
