#pragma once
#include "runtime.h"
#include <filesystem>
namespace seed {
void configure_local_vs(gbarecomp::RunOptions& options, const std::filesystem::path& art_path,
                        const std::string& jp_rom, const std::string& bios, const std::string& save,
                        int jp_debug_port = 0);
}
