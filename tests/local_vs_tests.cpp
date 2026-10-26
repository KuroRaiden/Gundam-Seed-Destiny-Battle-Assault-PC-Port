#include "local_vs.h"
#include <cstdio>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        using Mode = seed::MenuBattleMode;
        seed::VsMenuRoute route;
        require(route.observe(4, 0, 2) == Mode::None && route.observe(7, 0, 2) == Mode::None,
                "Preset VS was intercepted");
        route = {};
        require(route.observe(4, 1, 0) == Mode::None && route.observe(12, 0, 0) == Mode::Online,
                "Actual VS transition did not open online mode");
        require(route.observe(12, 0, 0) == Mode::None, "Online transition fired twice");
        route = {};
        require(route.observe(4, 2, 0) == Mode::None && route.observe(11, 0, 0) == Mode::Training,
                "Training transition did not enter JP selection");
        route = {};
        require(route.observe(4, 2, 2) == Mode::None && route.observe(7, 0, 0) == Mode::Local,
                "Actual Password menu transition did not enter local VS");
        require(route.observe(7, 0, 0) == Mode::None, "Password transition fired twice");
        route = {};
        require(route.observe(4, 2, 2) == Mode::None && route.observe(4, 0, 0) == Mode::None &&
                    route.observe(7, 0, 0) == Mode::None,
                "Moving away left a stale route");
        route = {};
        require(route.observe(9, 2, 2) == Mode::None && route.observe(7, 2, 2) == Mode::None,
                "Unrelated state-7 entry activated local VS");
        seed::MatchInputs inputs;
        seed::LocalVsSelection select;
        auto frame = [&](unsigned p1, unsigned p2) {
            inputs.advance(p1, p2);
            return select.advance(inputs);
        };
        frame(16, 32);
        require(select.seats[0].cursor == 3 && select.seats[1].cursor == 9,
                "Cursors were not independent");
        frame(0, 0);
        frame(1, 0);
        require(select.seats[0].ready && !select.seats[1].ready, "One confirm readied both seats");
        frame(16, 16);
        require(select.seats[0].cursor == 3 && select.seats[1].cursor == 10,
                "Ready player moved or blocked the other player");
        frame(0, 0);
        frame(2, 0);
        require(!select.seats[0].ready && !select.cancelled,
                "Cancel on a ready seat exited the match");
        frame(0, 0);
        frame(1, 1);
        require(!select.launch, "Launch did not wait for the confirmation interval");
        for (int i = 0; i < 16; ++i)
            frame(0, 0);
        frame(2, 0);
        require(!select.launch && !select.seats[0].ready,
                "Cancel during the ready interval did not work");
        frame(0, 0);
        frame(1, 0);
        for (int i = 0; i < 17; ++i)
            frame(0, 0);
        require(select.launch, "Both ready seats did not launch");
        select = {};
        inputs = {};
        frame(2, 0);
        require(select.cancelled && !select.launch, "Unready cancel did not leave selection");
        select = {};
        inputs = {};
        select.seats[0].cursor = 0;
        frame(32, 0);
        require(select.seats[0].cursor == 12, "Left wrap failed");
        frame(0, 0);
        frame(16, 0);
        require(select.seats[0].cursor == 0, "Right wrap failed");
        select = {};
        inputs = {};
        select.seats[0] = {6, 255, false};
        auto other = select;
        seed::MatchInputs mirror;
        frame(1, 1);
        mirror.advance(1, 1);
        other.advance(mirror);
        require(select.seats[0].fighter < 12 && select.seats[0].fighter == other.seats[0].fighter,
                "Random selection was invalid or nondeterministic");
        select = {};
        inputs = {};
        select.available_slots = (1u << 2) | (1u << 4);
        frame(16, 0);
        require(select.seats[0].cursor == 4, "Locked portrait was selectable");
        frame(0, 0);
        frame(16, 0);
        require(select.seats[0].cursor == 2, "Unlocked portrait wrap failed");
        select = {};
        inputs = {};
        select.jp_layout = true;
        select.seats = {{{13, 0, false}, {22, 7, false}}};
        frame(16, 32);
        require(select.seats[0].cursor == 14 && select.seats[0].fighter == 1 &&
                    select.seats[1].cursor == 21 && select.seats[1].fighter == 6,
                "JP horizontal grid movement failed");
        frame(0, 0);
        frame(64, 128);
        require(select.seats[0].cursor == 6 && select.seats[1].cursor == 29,
                "JP vertical movement failed");
        select.seats[0] = {0, 14, false};
        inputs = {};
        frame(1, 0);
        require(!select.seats[0].ready && select.seats[0].fighter == 14,
                "Unported JP fighter was aliased or confirmed");
        inputs = {};
        frame(32, 0);
        require(select.seats[0].cursor == 7 && select.seats[0].fighter == 5,
                "JP horizontal wrap did not skip empty cells");
        inputs = {};
        frame(64, 0);
        require(select.seats[0].cursor == 31 && select.seats[0].fighter == 9,
                "JP vertical wrap failed");
        select.seats[0] = {2, 19, false};
        inputs = {};
        frame(16, 0);
        require(select.seats[0].cursor == 5, "JP blank center cells were navigable");
        select.seats[0] = {11, 255, false};
        inputs = {};
        frame(64, 0);
        require(select.seats[0].cursor == 11,
                "JP random cell vertical navigation entered empty space");
        frame(0, 0);
        frame(1, 0);
        require(select.seats[0].ready && select.seats[0].fighter < 12,
                "JP random selection chose an unported fighter");
        select = {};
        inputs = {};
        select.jp_layout = select.training = true;
        select.available_fighters = 0x017FFFFFu;
        select.seats = {{{1, 13, false}, {0, 14, false}}};
        frame(0, 1);
        require(!select.seats[0].ready && !select.seats[1].ready,
                "Training required or accepted P2 selection input");
        frame(1, 0);
        require(select.seats[0].ready && !select.seats[1].ready,
                "P1 confirm selected both training fighters");
        frame(0, 0);
        frame(2, 0);
        require(!select.seats[0].ready && !select.cancelled,
                "Training dummy back did not return to P1 choice");
        frame(0, 0);
        frame(1, 0);
        frame(0, 0);
        frame(1, 0);
        require(select.seats[0].ready && select.seats[1].ready,
                "P1 could not choose JP-only training opponent");
        for (int i = 0; i < 18; ++i)
            frame(0, 0);
        require(select.launch, "Training did not launch with one controller");
        std::puts("PASS: Local/Online/Training menu routes, Preset isolation, two-seat selection "
                  "and single-player JP training selection.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
