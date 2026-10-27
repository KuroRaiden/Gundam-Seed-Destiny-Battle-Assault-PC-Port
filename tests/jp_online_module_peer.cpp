#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ms_palette_fixture.h"
#include "jp_combat.h"
#include "udp_transport.h"
#include "live_controls.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
namespace {
unsigned seat = 0, input_frame = 0;
bool windowed = false, finish_fence = false;
unsigned remap_frames = 0;
SDL_Window* title_window = nullptr;
std::string observed_title;
seed::LiveControls controls;
void inputs(std::uint16_t* out) {
    const auto frame = input_frame++;
    if (title_window)
        observed_title = SDL_GetWindowTitle(title_window);
    if (windowed && seat == 0 && (frame == 60 || frame == 110)) {
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.scancode = SDL_SCANCODE_F4;
        key.key.key = SDLK_F4;
        SDL_PushEvent(&key);
    }
    if (windowed && (frame == 20 || frame == 21 || frame == 22)) {
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        constexpr SDL_Scancode training_keys[]{SDL_SCANCODE_F6, SDL_SCANCODE_F2, SDL_SCANCODE_F3};
        key.key.scancode = training_keys[frame - 20];
        key.key.key = SDL_GetKeyFromScancode(key.key.scancode, SDL_KMOD_NONE, false);
        SDL_PushEvent(&key);
    }
    if (frame < 40)
        out[0] = seat ? 32 : 16;
    else {
        auto value = frame * 747796405u + seat * 2891336453u;
        value ^= value >> 16;
        const unsigned action = (value >> 4) % 10;
        out[0] = std::uint16_t(action < 2    ? 1
                               : action < 3  ? 2
                               : action == 3 ? 256
                               : action == 4 ? 512
                                             : 0);
    }
    out[1] = 0;
    if (controls.overlay_open) {
        ++remap_frames;
        out[0] = 0;
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        seed::JpCombatRequest request;
        seed::OnlineCombatResult result;
        std::string rom, bios, save;
        std::filesystem::path report, module;
        unsigned port = 29070, peer = 29071;
        request.qualification_frames = 240;
        request.fighters[0] = 13;
        request.fighters[1] = 14;
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            auto value = [&] {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing qualification argument");
                return std::string(argv[++i]);
            };
            if (flag == "--custom-colors") {
                request.palettes[0] = ms_test_palette(0);
                request.palettes[1] = ms_test_palette(1);
            } else if (flag == "--fighters") {
                request.fighters[0] = std::stoul(value());
                request.fighters[1] = std::stoul(value());
            } else if (flag == "--colors") {
                request.colors[0] = std::stoul(value());
                request.colors[1] = std::stoul(value());
            } else if (flag == "--seat")
                seat = std::stoul(value());
            else if (flag == "--net-bind")
                port = std::stoul(value());
            else if (flag == "--net-peer")
                peer = std::stoul(value());
            else if (flag == "--check-frames")
                request.qualification_frames = std::stoul(value());
            else if (flag == "--rom")
                rom = value();
            else if (flag == "--bios")
                bios = value();
            else if (flag == "--save")
                save = value();
            else if (flag == "--report")
                report = value();
            else if (flag == "--module")
                module = value();
            else if (flag == "--window")
                windowed = true;
            else if (flag == "--finish-fence")
                finish_fence = true;
            else if (flag != "--bios-hle")
                throw std::runtime_error("Unknown qualification argument: " + flag);
        }
        if (module.empty())
            module = std::filesystem::absolute(argv[0]).parent_path() / "SeedJpCombat.dll";
        if (seat > 1 || port > 65535 || !port || peer > 65535 || !peer ||
            request.qualification_frames < 3)
            throw std::runtime_error("Invalid peer qualification configuration");
        seed::UdpTransport::Token token{};
        for (unsigned i = 0; i < token.size(); ++i)
            token[i] = std::uint8_t(i * 17 + 3);
        seed::UdpTransport transport(std::uint16_t(port), "127.0.0.1", std::uint16_t(peer), token);
        request.connection = transport.borrow(seat, 0x53454544);
        request.surface.online = true;
        request.rom = rom.c_str();
        request.bios = bios.c_str();
        request.save = save.c_str();
        request.sample_inputs = inputs;
        request.online_result = &result;
        std::snprintf(request.player_names[0], sizeof(request.player_names[0]), "Kira");
        std::snprintf(request.player_names[1], sizeof(request.player_names[1]), "Athrun");
        if (finish_fence) {
            request.qualification_end_frame = request.qualification_frames;
            request.qualification_frames = 0;
        }
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        if (windowed) {
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
                throw std::runtime_error(SDL_GetError());
            window =
                SDL_CreateWindow(seat ? "Online P2 qualification" : "Online P1 / F4 qualification",
                                 960, 640, SDL_WINDOW_HIDDEN);
            renderer = SDL_CreateRenderer(window, nullptr);
            if (!window || !renderer)
                throw std::runtime_error(SDL_GetError());
            controls.path = std::filesystem::path(save).parent_path() / "controls-test.ini";
            title_window = window;
            request.surface.window = window;
            request.surface.renderer = renderer;
            request.surface.inputs = &controls;
        }
        const auto library = LoadLibraryW(module.c_str());
        const auto run =
            library
                ? reinterpret_cast<seed::RunJpCombat>(GetProcAddress(library, "SeedRunJpCombat"))
                : nullptr;
        if (!run)
            throw std::runtime_error("Cannot load the production JP combat module");
        const int code = run(&request);
        std::filesystem::create_directories(report.parent_path());
        std::ofstream out(report);
        out << "{\"engine\":\"native-jp\",\"transport\":\"udp\",\"backend\":\"GekkoNet\","
               "\"driver\":\"production-dll\",\"seat\":"
            << seat << ",\"frames\":" << result.frames
            << ",\"target\":" << request.qualification_frames
            << ",\"initial_crc\":" << result.initial_crc << ",\"final_crc\":" << result.final_crc
            << ",\"peer_crc\":" << result.peer_crc
            << ",\"converged\":" << (result.converged ? "true" : "false")
            << ",\"rollbacks\":" << result.rollbacks
            << ",\"replayed_frames\":" << result.replayed_frames
            << ",\"max_rollback\":" << result.max_rollback << ",\"stalls\":" << result.stalls
            << ",\"desyncs\":" << result.desyncs << ",\"ping_ms\":" << result.ping_ms
            << ",\"jitter_ms\":" << result.jitter_ms << ",\"remap_frames\":" << remap_frames
            << ",\"finish_fence\":" << (finish_fence ? "true" : "false") << ",\"title\":\""
            << observed_title << "\""
            << ",\"hp\":[" << result.hp[0] << "," << result.hp[1] << "]}\n";
        out.close();
        if (code || !result.converged)
            throw std::runtime_error(std::string("Production module qualification failed: ") +
                                     result.message);
        if (windowed && (observed_title.find("Kira vs Athrun | Delay 2f | Rollback ") != 0 ||
                         observed_title.find(" | Ping ") == std::string::npos))
            throw std::runtime_error(
                "Live window title did not show player names and network statistics");
        if (windowed && !seat && remap_frames < 40)
            throw std::runtime_error(
                "F4 remapping did not remain open while online frames advanced");
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        std::printf("PASS production DLL seat %u: %u frames, checksum %u, %u rollbacks\n", seat,
                    result.frames, result.final_crc, result.rollbacks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
