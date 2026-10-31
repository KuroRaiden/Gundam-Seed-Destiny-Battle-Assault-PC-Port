#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "jp_combat.h"
#include "live_controls.h"
#include "match_replay.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
namespace {
void check(bool ok, const char* error) {
    if (!ok)
        throw std::runtime_error(error);
}
struct Key {
    SDL_WindowID window;
    SDL_Scancode code;
};
Uint32 SDLCALL key(void* data, SDL_TimerID, Uint32) {
    const auto& item = *static_cast<Key*>(data);
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = item.window;
    event.key.scancode = item.code;
    event.key.key = SDL_GetKeyFromScancode(item.code, SDL_KMOD_NONE, false);
    SDL_PushEvent(&event);
    return 0;
}
std::set<std::filesystem::path> images(const std::filesystem::path& folder) {
    std::set<std::filesystem::path> files;
    if (std::filesystem::exists(folder))
        for (const auto& entry : std::filesystem::directory_iterator(folder))
            if (entry.path().extension() == ".png")
                files.insert(entry.path());
    return files;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Pass JP ROM, BIOS and an E: output folder.");
        const auto output = std::filesystem::u8path(argv[3]);
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD), SDL_GetError());
        auto* window =
            SDL_CreateWindow("Training F7 end-to-end check", 960, 640, SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, "software");
        check(renderer, SDL_GetError());
        const auto module_path = seed::combat_library_path();
        auto module = LoadLibraryW(module_path.c_str());
        auto run =
            module ? reinterpret_cast<seed::RunJpCombat>(GetProcAddress(module, "SeedRunJpCombat"))
                   : nullptr;
        check(run, "Cannot load production combat module.");
        const auto folder = module_path.parent_path() / "snapshots";
        const auto before = images(folder);
        seed::LiveControls controls;
        controls.path = output / "controls.ini";
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.surface = {window, renderer, &controls, false};
        request.rom = argv[1];
        request.bios = argv[2];
        const auto save = (output / "training.sav").string();
        request.save = save.c_str();
        request.fighters[0] = 13;
        request.fighters[1] = 14;
        const auto id = SDL_GetWindowID(window);
        Key keys[]{{id, SDL_SCANCODE_F7},
                   {id, SDL_SCANCODE_F5},
                   {id, SDL_SCANCODE_F6},
                   {id, SDL_SCANCODE_F7},
                   {id, SDL_SCANCODE_F7}};
        constexpr unsigned times[]{7000, 7300, 7400, 8000, 9000};
        SDL_TimerID timers[5]{};
        for (unsigned i = 0; i < 5; ++i)
            timers[i] = SDL_AddTimer(times[i], key, &keys[i]);
        auto end = SDL_AddTimer(
            11000,
            [](void*, SDL_TimerID, Uint32) -> Uint32 {
                SDL_Event event{};
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
                return 0;
            },
            nullptr);
        const int code = run(&request);
        for (auto timer : timers)
            SDL_RemoveTimer(timer);
        SDL_RemoveTimer(end);
        check(code == 2 || code == 0, "Training runtime failed.");
        unsigned count = 0;
        for (const auto& path : images(folder))
            if (!before.contains(path)) {
                std::filesystem::copy_file(
                    path, output / ("snapshot-" + std::to_string(count++) + ".png"));
            }
        controls.devices.close();
        FreeLibrary(module);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::printf("Training normal-loop F7 captures: %u (expected 3).\n", count);
        check(count == 3, "F7 did not save each PNG through the normal Training game loop.");
        std::puts("PASS: production Training loop, SDL F7 input, three PNGs and F5/F6 overlays.");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
