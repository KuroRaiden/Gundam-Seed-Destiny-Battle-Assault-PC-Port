#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "jp_combat.h"
#include "live_controls.h"
#include "match_replay.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned live_calls = 0;
void live_inputs(std::uint16_t* players) {
    ++live_calls;
    // Distinct values prove either chosen fighter uses the P1 control profile.
    players[0] = 17;
    players[1] = 512;
}
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Key {
    SDL_WindowID window;
    SDL_Scancode code;
};
Uint32 SDLCALL push_key(void* data, SDL_TimerID, Uint32) {
    const auto& key = *static_cast<Key*>(data);
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = key.window;
    event.key.scancode = key.code;
    event.key.key = SDL_GetKeyFromScancode(key.code, SDL_KMOD_NONE, false);
    SDL_PushEvent(&event);
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 6, "Pass scenario, replay, JP ROM, BIOS and a new E: scratch folder.");
        const std::string scenario = argv[1];
        require(scenario == "plain" || scenario == "boxes" || scenario == "boxes-off" ||
                    scenario == "takeover" || scenario == "takeover-close" ||
                    scenario == "takeover-end" || scenario == "launcher-1p" ||
                    scenario == "launcher-2p",
                "Unknown replay practice scenario.");
        const auto file = std::filesystem::u8path(argv[2]);
        const auto before = seed::replay_file_hash(file);
        const auto replay = seed::read_match_replay(file);
        require(replay.inputs.size() >= 480, "Use a replay containing at least 480 frames.");
        const auto folder = std::filesystem::u8path(argv[5]);
        require(std::filesystem::create_directories(folder), "Use a new scratch folder.");
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD), SDL_GetError());
        auto* window =
            SDL_CreateWindow("Replay practice qualification", 720, 480, SDL_WINDOW_HIDDEN);
        auto* renderer = window ? SDL_CreateRenderer(window, "software") : nullptr;
        require(window && renderer, SDL_GetError());
        seed::LiveControls controls;
        controls.path = folder / "test-controls.ini";
        const auto module = LoadLibraryW(seed::combat_library_path().c_str());
        const auto run =
            module ? reinterpret_cast<seed::RunJpCombat>(GetProcAddress(module, "SeedRunJpCombat"))
                   : nullptr;
        require(run, "Cannot load the combat module.");
        const auto path = file.string(), save = (folder / "unused.sav").string();
        const auto recordings = (folder / "unexpected-recordings").string();
        seed::JpCombatRequest request;
        request.rom = argv[3];
        request.bios = argv[4];
        request.save = save.c_str();
        request.replay_path = path.c_str();
        request.replay_directory = recordings.c_str();
        request.surface = {window, renderer, &controls, false};
        request.stage = replay.stage;
        request.sample_inputs = live_inputs;
        for (unsigned p = 0; p < 2; ++p) {
            request.fighters[p] = replay.fighters[p];
            request.colors[p] = replay.colors[p];
            request.palettes[p] = replay.palettes[p];
        }
        std::vector<std::pair<unsigned, SDL_Scancode>> schedule;
        if (scenario == "launcher-1p" || scenario == "launcher-2p") {
            request.replay_takeover_player = scenario == "launcher-1p" ? 0 : 1;
            request.replay_takeover_frame = 369;
            request.replay_boxes = true;
            schedule = {
                {5000, SDL_SCANCODE_SPACE}, {5600, SDL_SCANCODE_F10}, {6100, SDL_SCANCODE_F11}};
        } else if (scenario == "takeover") {
            schedule = {
                {6800, SDL_SCANCODE_SPACE}, {7100, SDL_SCANCODE_F8},  {7500, SDL_SCANCODE_F10},
                {7800, SDL_SCANCODE_F9},    {8100, SDL_SCANCODE_F4},  {8300, SDL_SCANCODE_F8},
                {8600, SDL_SCANCODE_F4},    {8900, SDL_SCANCODE_F10}, {9100, SDL_SCANCODE_F6},
                {9500, SDL_SCANCODE_F11},   {9700, SDL_SCANCODE_F6}};
        } else if (scenario == "takeover-close") {
            schedule = {
                {6800, SDL_SCANCODE_F8}, {7400, SDL_SCANCODE_F6}, {8000, SDL_SCANCODE_ESCAPE}};
        } else if (scenario == "takeover-end") {
            schedule = {
                {6800, SDL_SCANCODE_F8}, {10500, SDL_SCANCODE_F10}, {11200, SDL_SCANCODE_F11}};
        } else {
            // Training save slots must remain disabled in replay playback.
            schedule = {{1000, SDL_SCANCODE_F2}, {1200, SDL_SCANCODE_F3}};
            if (scenario != "plain") {
                schedule.push_back({1400, SDL_SCANCODE_SPACE});
                schedule.push_back({1600, SDL_SCANCODE_F6});
                schedule.push_back({2400, SDL_SCANCODE_SPACE});
            }
            if (scenario == "boxes-off")
                schedule.push_back({2200, SDL_SCANCODE_F6});
        }
        std::vector<Key> keys;
        keys.reserve(schedule.size());
        std::vector<SDL_TimerID> timers;
        for (const auto& [time, code] : schedule) {
            keys.push_back({SDL_GetWindowID(window), code});
            timers.push_back(SDL_AddTimer(time, push_key, &keys.back()));
            require(timers.back(), "Cannot schedule replay input.");
        }
        seed::ReplayPlaybackResult result;
        request.replay_result = &result;
        const auto code = run(&request);
        for (auto timer : timers)
            SDL_RemoveTimer(timer);
        require(code == 0, result.message);
        if (scenario == "takeover-close") {
            require(!result.verified && result.frames > 0 && live_calls > 0,
                    "Practice was incorrectly verified as an original replay.");
            require(std::string_view(result.message) == "Takeover practice closed.",
                    "Practice did not exit cleanly.");
        } else {
            require(result.verified && result.frames == replay.inputs.size(), result.message);
            require(!replay.complete || result.final_crc == replay.final_crc,
                    "Replay practice or collision display changed the original match result.");
            require((scenario.starts_with("takeover") || scenario.starts_with("launcher")) ==
                        (live_calls > 0),
                    "Live inputs were sampled in the wrong playback mode.");
        }
        require(seed::replay_file_hash(file) == before, "The source replay was rewritten.");
        require(!std::filesystem::exists(recordings), "Practice unexpectedly wrote a replay.");
        require(!std::filesystem::exists(save + ".training.state"),
                "Replay hotkeys wrote a Training save state.");
        auto* image = SDL_RenderReadPixels(renderer, nullptr);
        require(image, "Cannot capture replay practice.");
        const auto capture = (folder / "window.png").string();
        const bool saved = SDL_SavePNG(image, capture.c_str());
        SDL_DestroySurface(image);
        require(saved, "Cannot save replay practice capture.");
        std::printf("PASS: %s frames=%u live_calls=%u verified=%u source_unchanged=1 title=%s\n",
                    scenario.c_str(), result.frames, live_calls, unsigned(result.verified),
                    SDL_GetWindowTitle(window));
        controls.devices.close();
        FreeLibrary(module);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
