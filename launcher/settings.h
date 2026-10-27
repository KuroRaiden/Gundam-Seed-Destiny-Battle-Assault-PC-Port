#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace seed::launcher {
enum class FileKind { US, Japanese, Bios };
inline constexpr std::array<const char*, 3> kPortableGamePaths{
    "gamefiles/Mobile Suit Gundam Seed - Battle Assault (USA).gba",
    "gamefiles/Kidou Senshi Gundam - Seed Destiny (Japan).gba", "gamefiles/gba_bios.bin"};
struct GameFile {
    FileKind kind;
    std::string path;
    bool verified = false;
    std::string message = "Choose a file";
    void verify(const std::filesystem::path& base_directory = {});
    void edited() {
        verified = false;
        message = path.empty() ? "Choose a file" : "Verification required";
    }
};
struct Settings {
    std::array<GameFile, 3> files{{{FileKind::US, kPortableGamePaths[0]},
                                   {FileKind::Japanese, kPortableGamePaths[1]},
                                   {FileKind::Bios, kPortableGamePaths[2]}}};
    int scale = 3;
    bool fighter_outlines = true;
    int fighter_outline_opacity = 100;
    void load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path, std::string& error) const;
};
} // namespace seed::launcher
