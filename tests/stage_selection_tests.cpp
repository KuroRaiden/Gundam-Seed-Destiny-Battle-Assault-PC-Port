#include "local_vs.h"
#include <cstdio>
#include <stdexcept>
void require(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
int main() {
    try {
        for (unsigned cursor = 0; cursor <= seed::kJpStageCount; ++cursor) {
            seed::LocalVsSelection selection;
            selection.stage_enabled = true;
            selection.stage_cursor = cursor == seed::kJpStageCount ? seed::kRandomStage : cursor;
            seed::MatchInputs inputs;
            auto tick = [&](unsigned p1 = 0, unsigned p2 = 0) {
                inputs.advance(p1, p2);
                selection.advance(inputs);
            };
            tick(1, 1);
            for (unsigned i = 0; i < 30; ++i) tick(1, 1);
            require(selection.stage_active && !selection.launch, "Held A skipped stage select");
            tick();
            tick(1);
            require(selection.stage_confirmed && selection.launch && seed::valid_jp_stage(selection.stage),
                    "Stage confirmation failed");
            require(cursor == seed::kJpStageCount || selection.stage == cursor,
                    "A stage loaded a different ID");
        }
        seed::LocalVsSelection selection;
        selection.stage_enabled = selection.stage_active = true;
        seed::MatchInputs inputs;
        auto tick = [&](unsigned p1 = 0, unsigned p2 = 0) {
            inputs.advance(p1, p2);
            selection.advance(inputs);
        };
        tick(32);
        require(selection.stage_cursor == seed::kRandomStage, "Stage list did not wrap to Random");
        tick(); tick(32);
        require(selection.stage_cursor == 24, "Stage list skipped the last native stage");
        tick(); tick(2);
        require(!selection.stage_active && !selection.launch && !selection.seats[0].ready &&
                    !selection.seats[1].ready, "Back did not return to character select");
        selection.stage_active = true;
        selection.local_seat_mask = 2;
        tick(); tick(0, 16);
        require(selection.stage_cursor == 24, "Guest moved the host stage cursor");
        tick(); tick(0, 1);
        require(!selection.launch, "Guest confirmed the host stage");
        std::puts("PASS: all 25 stages, Random, held-confirm guard, Back and guest ownership");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
