#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "runtime.h"
#include "jp_asset_import.h"
#include "local_vs_runtime.h"
#include "invite_window.h"
#include "invite_code.h"
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

// US cartridge host with one-simulation local VS through Password Mode.
namespace {
void import_jp_art(const std::filesystem::path& base, const std::string& rom,
                   const std::string& bios, const std::filesystem::path& output) {
    const auto library = LoadLibraryW((base / "SeedJpCombat.dll").c_str());
    if (!library)
        throw std::runtime_error("The JP combat module is missing or could not be loaded.");
    const auto import = reinterpret_cast<seed::ImportJpSelectionArt>(
        GetProcAddress(library, "SeedImportJpSelectionArt"));
    const auto result = import ? import(rom.c_str(), bios.c_str(), output.string().c_str()) : 1;
    FreeLibrary(library);
    if (result)
        throw std::runtime_error(
            "Could not prepare JP art from your local game files. Check the game log.");
}
} // namespace
int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    options.builtin_game_name = "Gundam SEED: Battle Assault (US)";
    options.builtin_rom_sha1 = "8287d3d336d94a85c5cb22da0fd84918199197e1";
    options.diagnostic_replay_default = false;
    std::string jp_rom, bios, save, join_code, import_output;
    int jp_debug_port = 0;
    std::vector<char*> args{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--import-jp-assets" && i + 1 < argc) {
            import_output = argv[++i];
            continue;
        }
        if (flag == "--join-code" && i + 1 < argc) {
            join_code = argv[++i];
            continue;
        }
        if (flag == "--jp-rom" && i + 1 < argc) {
            jp_rom = argv[++i];
            continue;
        }
        if (flag == "--jp-tcp" && i + 1 < argc) {
            jp_debug_port = std::stoi(argv[++i]);
            continue;
        }
        if (flag == "--bios" && i + 1 < argc)
            bios = argv[i + 1];
        if (flag == "--save" && i + 1 < argc)
            save = argv[i + 1];
        args.push_back(argv[i]);
    }
    if (save.empty())
        save = (std::filesystem::absolute(argv[0]).parent_path() / "local-vs-jp.sav").string();
    else
        save += ".jp-local.sav";
    try {
        const auto base = std::filesystem::absolute(argv[0]).parent_path();
        const auto art = base / "assets" / "destiny-selection.bin";
        if (!import_output.empty()) {
            import_jp_art(base, jp_rom, bios, std::filesystem::u8path(import_output));
            return 0;
        }
        if (!std::filesystem::is_regular_file(art))
            import_jp_art(base, jp_rom, bios, art);
        if (!join_code.empty())
            seed::queue_game_invite(join_code);
        seed::configure_local_vs(options,
                                 std::filesystem::absolute(argv[0]).parent_path() / "assets" /
                                     "destiny-selection.bin",
                                 jp_rom, bios, save, jp_debug_port);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
    return gbarecomp::run_game(int(args.size()), args.data(), options);
}
