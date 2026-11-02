#include "jp_combat_runtime.h"
#include "training_tools.h"
#include "training_challenges.h"
#include "training_meter_overlay.h"
#include "frame_control.h"
#include "runtime_arm.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
namespace {
std::filesystem::path output;
bool roster_test = false;
unsigned opponent_filter = 0;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void key(SDL_Scancode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = code;
    seed::training_tools_event(event, false);
}
void capture(gbarecomp::NativeFrameControl& machine) {
    require(SDL_InitSubSystem(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("Guard Cancel challenge UI", 960, 640, SDL_WINDOW_HIDDEN);
    auto* renderer = window ? SDL_CreateRenderer(window, "software") : nullptr;
    require(window && renderer, SDL_GetError());
    auto* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, 240, 160);
    require(texture && SDL_UpdateTexture(texture, nullptr, machine.pixels(), 240 * 3),
            SDL_GetError());
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    {
        seed::TrainingMeterOverlay overlay;
        overlay.initialize(window, renderer);
        for (unsigned i = 0; i < 3; ++i) {
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            overlay.draw(false);
        }
        auto* surface = SDL_RenderReadPixels(renderer, nullptr);
        require(surface && SDL_SavePNG(surface, (output / "challenge.png").string().c_str()),
                SDL_GetError());
        SDL_DestroySurface(surface);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        auto* plain = SDL_RenderReadPixels(renderer, nullptr);
        overlay.draw(false, true); // clean F7 capture omits the challenge card
        auto* clean = SDL_RenderReadPixels(renderer, nullptr);
        require(plain && clean && plain->pitch == clean->pitch && plain->h == clean->h &&
                    std::memcmp(plain->pixels, clean->pixels, plain->pitch * plain->h) == 0,
                "Challenge card leaked into a clean snapshot");
        SDL_DestroySurface(plain);
        SDL_DestroySurface(clean);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
int exercise(gbarecomp::NativeFrameControl& machine, unsigned opponent) {
    namespace jp = seed::address::jp;
    const auto p1 = jp::kBattleContext + jp::kFighterRecordsOffset;
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    auto advance = [&](unsigned input = 0) {
        seed::set_jp_frame_inputs(input, 0);
        require(machine.advance(1023 ^ input), "Native drill advance failed");
        machine.drain_audio(audio);
        if (seed::training_checkpoint_pending())
            seed::training_service_checkpoint(machine);
    };
    for (unsigned i = 0; i < 600; ++i)
        advance();
    require(seed::training_view().ready, "Training not initialized");
    std::vector<std::uint8_t> before, after;
    std::string error;
    require(machine.save(before, error), error.c_str());
    key(SDL_SCANCODE_F8);
    seed::training_service_checkpoint(machine);
    std::ofstream trace(output / ("trace-" + std::to_string(opponent) + ".csv"));
    trace << "drill,frame,state,motion,input,success,attempt,ps,pressed,held,timer,guardframe,"
             "facing,x1,x2,p2state,status\n";
    for (unsigned drill = 0; drill < 4; ++drill) {
        if (drill) {
            key(SDL_SCANCODE_F8);
            seed::training_service_checkpoint(machine);
        }
        bool success = false;
        unsigned previous_input = 0;
        for (unsigned frame = 0; frame < 600; ++frame) {
            const auto state = bus_read_u16(p1);
            unsigned input = 32; // P1 starts on the left: back to block.
            if ((state == 3 || state == 4) && !(previous_input & (drill < 2 ? 256 : 3)) &&
                bus_read_u8(p1 + 88) && bus_read_u16(p1 + 80))
                input = drill == 0   ? 32 | 256
                        : drill == 1 ? 16 | 256
                        : drill == 2 ? 16 | 1
                                     : 16 | 2;
            advance(input);
            previous_input = input;
            const auto view = seed::guard_challenge_view();
            if (!roster_test && drill == 0 && frame == 85)
                capture(machine);
            trace << drill << ',' << frame << ',' << bus_read_u16(p1) << ','
                  << unsigned(bus_read_u8(p1 + 14)) << ',' << input << ',' << view.successes << ','
                  << view.attempts << ',' << bus_read_u16(p1 + 12) << ',' << bus_read_u16(p1 + 22)
                  << ',' << bus_read_u16(p1 + 24) << ',' << unsigned(bus_read_u8(p1 + 88)) << ','
                  << bus_read_u16(p1 + 80) << ',' << bus_read_u16(p1 + 74) << ','
                  << std::int32_t(bus_read_u32(p1 + 36)) / 65536 << ','
                  << std::int32_t(bus_read_u32(p1 + 96 + 36)) / 65536 << ','
                  << bus_read_u16(p1 + 96) << ',' << std::quoted(std::string(view.status)) << '\n';
            if (view.successes) {
                success = true;
                break;
            }
            if (view.status.starts_with("You were hit") || view.status.starts_with("Time up"))
                break;
        }
        require(
            success,
            "Native Guard Cancel drill did not recognize the actual technique; inspect trace.csv");
        const auto attempts = seed::guard_challenge_view().attempts;
        key(SDL_SCANCODE_F9);
        seed::training_service_checkpoint(machine);
        require(seed::guard_challenge_view().attempts == attempts + 1,
                "F9 did not retry the RAM anchor");
        for (unsigned i = 0; i < 8; ++i)
            advance(1 | 256); // not a valid guarded cancel
        require(seed::guard_challenge_view().successes == 1, "A raw button press awarded success");
        std::cout << "PASS native Guard Cancel drill " << drill + 1 << '\n';
    }
    if (!roster_test && !opponent_filter) {
        for (unsigned goal = 2; goal <= 5; ++goal) {
            key(SDL_SCANCODE_F9);
            seed::training_service_checkpoint(machine);
            unsigned previous = 0;
            for (unsigned frame = 0; frame < 600; ++frame) {
                const auto state = bus_read_u16(p1);
                const auto input = (state == 3 || state == 4) && !(previous & 2) &&
                                           bus_read_u8(p1 + 88) && bus_read_u16(p1 + 80)
                                       ? 18u
                                       : 32u;
                advance(input);
                previous = input;
                if (seed::guard_challenge_view().successes == goal)
                    break;
            }
            require(seed::guard_challenge_view().successes == goal,
                    "Five-success goal did not advance");
        }
        require(seed::guard_challenge_view().status.starts_with("Challenge complete"),
                "The goal never completed after five real native cancels");
    }
    key(SDL_SCANCODE_F8); // back to the backward-escape drill
    seed::training_service_checkpoint(machine);
    unsigned previous = 0;
    for (unsigned frame = 0; frame < 600; ++frame) {
        const auto state = bus_read_u16(p1);
        const unsigned input = (state == 3 || state == 4) && !(previous & 256) &&
                                       bus_read_u8(p1 + 88) && bus_read_u16(p1 + 80)
                                   ? 64 | 256
                                   : 32;
        advance(input);
        previous = input;
        if (seed::guard_challenge_view().status.starts_with("A different"))
            break;
    }
    require(seed::guard_challenge_view().drill == 0 &&
                seed::guard_challenge_view().successes == 1 &&
                seed::guard_challenge_view().status.starts_with("A different"),
            "An upward native escape incorrectly completed the backward-escape drill");
    key(SDL_SCANCODE_F10);
    require(seed::training_service_checkpoint(machine), "F10 did not load the original situation");
    require(!seed::guard_challenge_active(), "F10 left the drill active");
    require(machine.save(after, error), error.c_str());
    require(before == after, "Exiting drills did not restore the exact original machine");
    key(SDL_SCANCODE_F2); // same-pump ordering must not replace a manual slot
    key(SDL_SCANCODE_F8);
    seed::training_service_checkpoint(machine);
    seed::training_service_checkpoint(machine);
    require(seed::guard_challenge_active() && !seed::training_checkpoint_pending(),
            "A queued manual save escaped the challenge's slot isolation");
    key(SDL_SCANCODE_F10);
    seed::training_service_checkpoint(machine);
    require(machine.save(after, error) && before == after,
            "The save-slot edge case changed the original Training state");
    seed::configure_training_tools(false, true, output / "offline-only.sav");
    key(SDL_SCANCODE_F8);
    require(!seed::guard_challenge_pending() && !seed::guard_challenge_active(),
            "Online challenge was enabled");
    return 0;
}
int drive(gbarecomp::NativeFrameControl& machine) {
    if (!roster_test && !opponent_filter)
        return exercise(machine, 8);
    for (unsigned opponent = 1; opponent <= 27; ++opponent) {
        if (opponent_filter && opponent != opponent_filter)
            continue;
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.fighters[0] = 13;
        request.fighters[1] = opponent;
        const auto save = (output / ("opponent-" + std::to_string(opponent) + ".sav")).string();
        request.save = save.c_str();
        gbarecomp::RunOptions ignored;
        seed::configure_jp_combat(ignored, request);
        exercise(machine, opponent);
        std::cout << "PASS all four drills against native opponent " << opponent << '\n'
                  << std::flush;
    }
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4 || argc == 5,
                "Pass JP ROM, BIOS, new E: output folder and optionally roster");
        roster_test = argc == 5 && std::string_view(argv[4]) == "roster";
        if (argc == 5 && !roster_test)
            opponent_filter = std::stoul(argv[4]);
        output = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(output);
        seed::JpCombatRequest request;
        request.mode = seed::JpCombatMode::Training;
        request.fighters[0] = 13;
        request.fighters[1] = 8;
        const auto save = (output / "drills.sav").string();
        request.save = save.c_str();
        gbarecomp::RunOptions options;
        seed::configure_jp_combat(options, request);
        options.native_frame_driver = drive;
        std::vector<std::string> values{argv[0], "--rom",      argv[1],  "--bios",
                                        argv[2], "--bios-hle", "--save", save};
        std::vector<char*> args;
        for (auto& s : values)
            args.push_back(s.data());
        return gbarecomp::run_game(int(args.size()), args.data(), options);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
