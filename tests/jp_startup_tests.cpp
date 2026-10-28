#include "jp_combat_runtime.h"
#include "frame_control.h"
#include "game_addresses.h"
#include "runtime_arm.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
int check(gbarecomp::NativeFrameControl& machine) {
    namespace jp = seed::address::jp;
    machine.seed_clock(946684800);
    std::vector<gbarecomp::NativeFrameControl::AudioBlock> audio;
    std::vector<std::uint8_t> boot, ready;
    std::string error;
    require(machine.save(boot, error), error);
    unsigned cases = 0, maximum = 0;
    for (unsigned item = 0; item < 52; ++item) {
        gbarecomp::RunOptions ignored;
        seed::JpCombatRequest original;
        seed::configure_jp_combat(ignored, original);
        require(machine.load(boot, error), error);
        seed::JpCombatRequest request;
        request.fighters[0] = item < 27 ? item + 1 : 13;
        request.fighters[1] = item < 27 ? ((item + 1) % 27 + 1) : 14;
        request.stage = item < 27 ? 4 : std::uint8_t(item - 27);
        seed::configure_jp_combat(ignored, request);
        unsigned frames = 0;
        do {
            seed::set_jp_frame_inputs(0, 0);
            require(machine.advance(1023), "Native startup stopped");
            machine.drain_audio(audio);
            ++frames;
        } while (!seed::jp_combat_startup_ready() && frames < 600);
        require(frames < 200, "Startup advanced too far before synchronizing");
        require(seed::jp_combat_startup_ready(), "Battle never became ready");
        require(bus_read_u8(jp::kRoundPhase) == 0,
                "Synchronization skipped the native round intro");
        require((bus_read_u8(jp::kBattleContext + jp::kBattleStageOffset) & 31) == request.stage,
                "Wrong initial stage");
        require(machine.save(ready, error), error);
        require(machine.load(ready, error), error);
        maximum = frames > maximum ? frames : maximum;
        ++cases;
    }
    std::printf("PASS: %u native startup cases, all 27 fighters and 25 stages, round phase zero; "
                "maximum %u warm-up frames.\n",
                cases, maximum);
    return 0;
}
int main(int argc, char** argv) {
    try {
        gbarecomp::RunOptions options;
        seed::JpCombatRequest request;
        seed::configure_jp_combat(options, request);
        options.native_frame_driver = check;
        return gbarecomp::run_game(argc, argv, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
