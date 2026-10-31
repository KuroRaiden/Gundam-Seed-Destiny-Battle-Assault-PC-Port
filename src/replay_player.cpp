#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "replay_player.h"
#include "match_replay.h"
#include "jp_combat.h"
#include "live_controls.h"
#include "brand_icons.h"
#include "controller_db.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace seed {
int watch_match_replay(const std::filesystem::path& file, const std::string& rom,
                       const std::string& bios, int scale) {
    const auto replay = read_match_replay(file);
    const auto library = combat_library_path();
    if (replay.combat_hash != replay_file_hash(library))
        throw std::runtime_error("This replay needs its original game build: " +
                                 replay.build_version);
    if (replay.bios_hash != replay_file_hash(std::filesystem::u8path(bios)))
        throw std::runtime_error("This replay needs the same BIOS it was recorded with.");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        throw std::runtime_error(SDL_GetError());
    struct Owner {
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        HMODULE module = nullptr;
        ~Owner() {
            if (module)
                FreeLibrary(module);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            SDL_Quit();
        }
    } owner;
    scale = std::clamp(scale, 1, 10);
    owner.window = SDL_CreateWindow("Gundam SEED - Replay", 240 * scale, 160 * scale,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!owner.window)
        throw std::runtime_error(SDL_GetError());
    set_game_window_icon(owner.window);
    owner.renderer = SDL_CreateRenderer(owner.window, nullptr);
    if (!owner.renderer)
        owner.renderer = SDL_CreateRenderer(owner.window, "software");
    if (!owner.renderer)
        throw std::runtime_error(SDL_GetError());
    const auto root = library.parent_path();
    load_controller_db(root);
    LiveControls controls;
    controls.load(root / "config/controls.ini");
    owner.module = LoadLibraryW(library.c_str());
    const auto run =
        owner.module
            ? reinterpret_cast<RunJpCombat>(GetProcAddress(owner.module, "SeedRunJpCombat"))
            : nullptr;
    if (!run)
        throw std::runtime_error(
            "Could not load the replay battle system. Extract the complete build.");
    JpCombatRequest request;
    const auto playback_file = file.string(), save = (root / "replay-session.sav").string();
    request.rom = rom.c_str();
    request.bios = bios.c_str();
    request.save = save.c_str();
    request.replay_path = playback_file.c_str();
    request.stage = replay.stage;
    request.surface = {owner.window, owner.renderer, &controls, false};
    for (unsigned p = 0; p < 2; ++p) {
        request.fighters[p] = replay.fighters[p];
        request.colors[p] = replay.colors[p];
        request.palettes[p] = replay.palettes[p];
        std::snprintf(request.player_names[p], sizeof(request.player_names[p]), "%s",
                      replay.names[p].c_str());
    }
    ReplayPlaybackResult result;
    request.replay_result = &result;
    const int code = run(&request);
    controls.devices.close();
    std::fprintf(stderr, "replay_playback: frames=%u verified=%u crc=%u %s\n", result.frames,
                 unsigned(result.verified), result.final_crc, result.message);
    if (code < 0)
        throw std::runtime_error(*result.message ? result.message
                                                 : "Replay playback failed. Check the replay log.");
    return 0;
}
} // namespace seed
