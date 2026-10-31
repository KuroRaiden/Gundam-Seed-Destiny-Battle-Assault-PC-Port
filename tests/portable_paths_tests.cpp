#include "portable_paths.h"
#include <cstdio>
#include <stdexcept>
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
void put(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide an empty scratch folder");
        const auto root = std::filesystem::path(argv[1]);
        check(!std::filesystem::exists(root), "Scratch folder exists");
        std::filesystem::create_directories(root);
        put(root / "launcher-settings.ini", "us \"gamefiles/my.gba\"\nscale \"5\"\n");
        const auto personal = seed::file_text(root / "launcher-settings.ini");
        put(root / "config/launcher-settings.ini",
            "us \"gamefiles/Mobile Suit Gundam Seed - Battle Assault (USA).gba\"\njapanese "
            "\"gamefiles/Kidou Senshi Gundam - Seed Destiny (Japan).gba\"\nbios "
            "\"gamefiles/gba_bios.bin\"\nscale \"3\"\n");
        for (const auto name :
             {"controls.ini", "ms-colors.ini", "rom.cfg", "test.toml.frag", "player-name.txt"})
            put(root / name, "personal");
        put(root / "session.log", "diagnostics");
        put(root / "Haro.ico", "icon");
        put(root / "game-us.sav", "save");
        put(root / "gamefiles/my.gba", "game");
        put(root / "assets/cache.bin", "cache");
        put(root / "config/conflict.ini", "new");
        put(root / "conflict.ini", "old");
        put(root / "licenses/test.txt", "notice");
        put(root / "resource/licenses/conflict.txt", "new license");
        put(root / "licenses/conflict.txt", "old license");
        seed::prepare_portable_folders(root);
        check(std::filesystem::is_directory(root / "replays"), "Replay folder was not created");
        check(seed::file_text(root / "config/launcher-settings.ini") == personal,
              "Personal settings masked by defaults");
        for (const auto name :
             {"controls.ini", "ms-colors.ini", "rom.cfg", "test.toml.frag", "player-name.txt"})
            check(seed::file_text(root / "config" / name) == "personal",
                  "Setting moved incorrectly");
        check(seed::file_text(root / "config/conflict.ini") == "new" &&
                  seed::file_text(root / "config/migration-backups/conflict.ini") == "old",
              "Conflict lost data");
        check(seed::file_text(root / "Logs/session.log") == "diagnostics" &&
                  seed::file_text(root / "resource/Haro.ico") == "icon",
              "Resource/log migration failed");
        check(seed::file_text(root / "resource/licenses/test.txt") == "notice" &&
                  !std::filesystem::exists(root / "licenses"),
              "License relocation failed");
        check(seed::file_text(root / "resource/licenses/previous/conflict.txt") == "old license",
              "Older license lost");
        check(seed::file_text(root / "game-us.sav") == "save" &&
                  seed::file_text(root / "gamefiles/my.gba") == "game" &&
                  seed::file_text(root / "assets/cache.bin") == "cache",
              "Game data changed");
        check(seed::settings_root(root / "config/launcher-settings.ini") == root,
              "Relative game paths shifted to config");
        seed::prepare_portable_folders(root);
        check(seed::file_text(root / "config/launcher-settings.ini") == personal,
              "Repeated migration changed settings");
        std::puts("PASS: organized folders, personal/default conflict, relative path base, repeat "
                  "migration and unchanged saves/inputs/caches.");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
