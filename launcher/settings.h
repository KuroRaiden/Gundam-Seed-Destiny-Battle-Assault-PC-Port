#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace seed::launcher {
enum class FileKind { US, Japanese, Bios };
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
    std::array<GameFile, 3> files{{{FileKind::US}, {FileKind::Japanese}, {FileKind::Bios}}};
    int scale = 3;
    void load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path, std::string& error) const;
};
} // namespace seed::launcher
