#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "match_replay.h"
#include "jp_combat.h"
#include "live_controls.h"
#include "ms_palette_fixture.h"
#include <cstdio>
#include <stdexcept>
namespace {
unsigned frame = 0;
bool playback = false;
void inputs(std::uint16_t* out) {
    if (playback)
        throw std::runtime_error("Live controls reached replay simulation.");
    out[0] = frame % 90 < 30 ? 16 : frame % 17 < 2 ? 1 : 0;
    out[1] = frame % 90 < 30 ? 32 : frame % 23 < 3 ? 256 : 0;
    ++frame;
}
void check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 5 &&
            (std::string(argv[1]) == "--watch" || std::string(argv[1]) == "--watch-window")) {
            const bool windowed = std::string(argv[1]) == "--watch-window";
            const auto file = std::filesystem::u8path(argv[2]);
            const auto replay = seed::read_match_replay(file);
            HMODULE module = LoadLibraryW(seed::combat_library_path().c_str());
            const auto run =
                module
                    ? reinterpret_cast<seed::RunJpCombat>(GetProcAddress(module, "SeedRunJpCombat"))
                    : nullptr;
            check(run != nullptr, "Cannot load the combat module.");
            const auto path = file.string(),
                       save = (file.parent_path() / "playback-unused.sav").string();
            seed::JpCombatRequest request;
            request.rom = argv[3];
            request.bios = argv[4];
            request.save = save.c_str();
            request.replay_path = path.c_str();
            request.stage = replay.stage;
            SDL_Window* window = nullptr;
            SDL_Renderer* renderer = nullptr;
            seed::LiveControls controls;
            struct ScheduledKey {
                SDL_WindowID window;
                SDL_Scancode scancode;
            };
            std::array<ScheduledKey, 6> keys;
            std::array<SDL_TimerID, 6> timers{};
            if (windowed) {
                check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD), SDL_GetError());
                window = SDL_CreateWindow("Replay UI qualification", 720, 480, SDL_WINDOW_HIDDEN);
                renderer = SDL_CreateRenderer(window, "software");
                check(window && renderer, SDL_GetError());
                controls.path = file.parent_path() / "ui-test-controls.ini";
                request.surface = {window, renderer, &controls, false};
                constexpr SDL_Scancode sequence[]{SDL_SCANCODE_F1,    SDL_SCANCODE_F1,
                                                  SDL_SCANCODE_F4,    SDL_SCANCODE_F4,
                                                  SDL_SCANCODE_SPACE, SDL_SCANCODE_SPACE};
                constexpr unsigned times[]{2000, 2200, 3000, 3200, 4000, 4200};
                for (unsigned i = 0; i < 6; ++i) {
                    keys[i] = {SDL_GetWindowID(window), sequence[i]};
                    timers[i] = SDL_AddTimer(
                        times[i],
                        [](void* context, SDL_TimerID, Uint32) -> Uint32 {
                            const auto& key = *static_cast<ScheduledKey*>(context);
                            SDL_Event event{};
                            event.type = SDL_EVENT_KEY_DOWN;
                            event.key.windowID = key.window;
                            event.key.scancode = key.scancode;
                            event.key.key =
                                SDL_GetKeyFromScancode(key.scancode, SDL_KMOD_NONE, false);
                            SDL_PushEvent(&event);
                            return 0;
                        },
                        &keys[i]);
                }
            }
            for (unsigned p = 0; p < 2; ++p) {
                request.fighters[p] = replay.fighters[p];
                request.colors[p] = replay.colors[p];
                request.palettes[p] = replay.palettes[p];
            }
            seed::ReplayPlaybackResult result;
            request.replay_result = &result;
            check(run(&request) == 0 && result.verified && result.frames == replay.inputs.size(),
                  result.message);
            check(!replay.complete || result.final_crc == replay.final_crc,
                  "Online replay final checksum differed.");
            std::printf("PASS: %s replay in fresh process, %u frames, checksum %u.\n",
                        replay.online ? "online" : "local", result.frames, result.final_crc);
            for (const auto timer : timers)
                if (timer)
                    SDL_RemoveTimer(timer);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            controls.devices.close();
            if (windowed)
                SDL_Quit();
            FreeLibrary(module);
            return 0;
        }
        check(argc == 4, "Pass JP ROM, BIOS and a new E: scratch directory.");
        const auto root = std::filesystem::u8path(argv[3]);
        std::filesystem::create_directories(root);
        HMODULE module = LoadLibraryW(seed::combat_library_path().c_str());
        const auto run =
            module ? reinterpret_cast<seed::RunJpCombat>(GetProcAddress(module, "SeedRunJpCombat"))
                   : nullptr;
        check(run != nullptr, "Cannot load the combat module.");
        unsigned cases = 0;
        for (const auto pair : {std::array<unsigned, 3>{1, 8, 0}, {13, 14, 24}, {24, 27, 7}}) {
            const auto folder = root / std::to_string(cases);
            std::filesystem::create_directories(folder);
            const auto directory = folder.string(), save = (folder / "unused.sav").string();
            seed::JpCombatRequest request;
            request.rom = argv[1];
            request.bios = argv[2];
            request.save = save.c_str();
            request.replay_directory = directory.c_str();
            request.qualification_frames = 480;
            request.sample_inputs = inputs;
            request.fighters[0] = pair[0];
            request.fighters[1] = pair[1];
            request.stage = pair[2];
            request.colors[0] = 1;
            request.palettes[0] = ms_test_palette(0);
            request.palettes[1] = ms_test_palette(1);
            seed::ReplayPlaybackResult recorded, watched;
            request.replay_result = &recorded;
            frame = 0;
            playback = false;
            check(run(&request) == 0 && recorded.frames == 480, "Native local recording failed.");
            std::filesystem::path file;
            for (const auto& entry : std::filesystem::directory_iterator(folder))
                if (entry.path().extension() == ".seedreplay") {
                    check(file.empty(), "Recorded twice.");
                    file = entry.path();
                }
            check(!file.empty(), "No replay file was saved.");
            const auto replay = seed::read_match_replay(file);
            check(replay.complete && replay.inputs.size() == 480 &&
                      replay.final_crc == recorded.final_crc,
                  "Native recording omitted frames or its result.");
            const auto replay_path = file.string();
            request.replay_path = replay_path.c_str();
            request.replay_result = &watched;
            playback = true;
            const auto result = run(&request);
            if (result != 0)
                std::fprintf(stderr, "Playback failed: %s\n", watched.message);
            check(result == 0 && watched.verified && watched.frames == 480 &&
                      watched.final_crc == recorded.final_crc,
                  "Replay did not reproduce the recorded machine state.");
            unsigned count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                count += entry.path().extension() == ".seedreplay";
                check(entry.path().filename().string().find(".sav.match-") == std::string::npos,
                      "Temporary save remained.");
            }
            check(count == 1, "Watching a replay created another replay.");
            auto incompatible = replay;
            incompatible.bios_hash = std::string(64, '0');
            const auto wrong = seed::save_match_replay(folder, incompatible).string();
            request.replay_path = wrong.c_str();
            watched = {};
            check(run(&request) < 0 && watched.frames == 0 &&
                      std::string(watched.message).find("BIOS") != std::string::npos,
                  "Incompatible replay did not fail clearly before simulation.");
            ++cases;
        }
        FreeLibrary(module);
        std::printf("PASS: %u real native record/playback cases, exact final machine checksums, "
                    "custom palettes, "
                    "hidden fighters, stages, ignored live controls, no re-recording, isolated "
                    "saves and incompatible BIOS rejection.\n",
                    cases);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
