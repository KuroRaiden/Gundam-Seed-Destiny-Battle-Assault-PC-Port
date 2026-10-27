#include "host_window.h"
#include "combat_host.h"
#include "online_lobby.h"
#include "invite_code.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <filesystem>

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void key(SDL_Scancode code) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = code;
    e.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    require(SDL_PushEvent(&e), "Key injection failed");
}
int main(int argc, char** argv) {
    try {
        // Capture this exact ImGui component with a hidden software surface so
        // visual QA does not require foregrounding or disturbing another app.
        if (argc >= 2) {
            require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL init failed");
            const int width = argc > 2 ? 720 : 960, height = argc > 2 ? 480 : 640;
            auto* hidden = SDL_CreateWindow("Lobby capture", width, height, SDL_WINDOW_HIDDEN);
            auto* canvas = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
            auto* renderer = SDL_CreateSoftwareRenderer(canvas);
            require(hidden && canvas && renderer, "Capture surface failed");
            seed::LiveControls controls;
            seed::OnlineLobbyState state;
            state.begin();
            if (argc > 2 && std::string(argv[2]) == "queue") {
                state.quick.phase = seed::QuickMatchPhase::Searching;
                state.quick.searching = true;
                state.quick.waiting = 5;
                state.quick.eligible = 3;
                state.quick_region = 1;
                state.quick.status = "Looking for an opponent...";
            } else if (argc > 2) {
                seed::JoinCode code;
                code.public_endpoint = {0x0100007f, 45678};
                code.local_port = 45678;
                code.token[0] = 42;
                state.code = seed::encode_join_code(code);
                state.invitation = seed::invitation_link(code);
                state.hosting = true;
                state.status = "Code ready. Share it with your opponent.";
            }
            seed::OnlineLobby ui;
            ui.initialize(hidden, renderer, &controls);
            ui.show(&state);
            for (int i = 0; i < 3; ++i) {
                SDL_SetRenderDrawColor(renderer, 35, 40, 55, 255);
                SDL_RenderClear(renderer);
                ui.draw();
            }
            std::filesystem::create_directories(std::filesystem::path(argv[1]).parent_path());
            SDL_FlushRenderer(renderer);
            require(SDL_SavePNG(canvas, argv[1]), "Lobby capture failed");
            ui.shutdown();
            SDL_DestroyRenderer(renderer);
            SDL_DestroySurface(canvas);
            SDL_DestroyWindow(hidden);
        }
        gbarecomp::HostWindow window;
        require(window.open(4, 240, 160, "Online VS lobby verification"), "Host open failed");
        std::vector<std::uint8_t> image(240 * 160 * 3, 40);
        auto surface = seed::suspend_menu_host();
        seed::resume_menu_host();
        seed::OnlineLobbyState lobby;
        lobby.begin();
        seed::show_online_lobby(&lobby);
        require(surface.inputs->overlay_open, "Lobby did not capture gameplay input");
        key(SDL_SCANCODE_F4);
        auto event = window.pump();
        window.present(image.data());
        require(!event.load_slot && !event.quit && !event.remap_pause,
                "Lobby leaked F4 to game/remapping");
        std::uint16_t players[2]{1023, 1023};
        seed::sample_host_players(players);
        require(!players[0] && !players[1], "Lobby passed gameplay input");
        key(SDL_SCANCODE_ESCAPE);
        key(SDL_SCANCODE_X);
        event = window.pump();
        require(lobby.cancelled && !lobby.open && !event.quit,
                "Esc did not cancel the lobby cleanly");
        require(!surface.inputs->overlay_open && surface.inputs->wait_for_release[0] &&
                    event.keyinput == 0x3FF,
                "Lobby let a closing-frame confirm escape its release guard");
        lobby.begin();
        seed::show_online_lobby(&lobby);
        window.present(image.data());
        SDL_Event pad{};
        pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pad.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
        SDL_PushEvent(&pad);
        require(!window.pump().quit && lobby.cancelled, "Gamepad B did not cancel the lobby");
        lobby.begin();
        seed::show_online_lobby(&lobby);
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        require(window.pump().quit, "Lobby swallowed window close");
        seed::show_online_lobby(nullptr);
        window.close();
        SDL_Quit();
        std::puts("PASS: ImGui lobby lifecycle, input/shortcut isolation, Esc/gamepad cancel, "
                  "release guards and window close.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
