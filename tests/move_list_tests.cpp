#include "move_list.h"
#include <cstdio>
#include <stdexcept>
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    try {
        const auto roster = seed::fighter_movelist();
        check(roster.size() == 27, "Roster is incomplete");
        for (unsigned i = 0; i < roster.size(); ++i) {
            const auto& fighter = roster[i];
            check(fighter.id == i + 1 && seed::fighter_moves(i + 1) == &fighter,
                  "Native ID mismatch");
            check(!fighter.name.empty() && !fighter.pilot.empty() && !fighter.berserker.empty(),
                  "Missing fighter metadata");
            bool ultimate = false;
            for (const auto& move : fighter.moves) {
                check(!move.command.empty(), "Empty input");
                for (const auto token : move.command)
                    check(std::string_view("UDBFablr>").find(token) != std::string_view::npos,
                          "Invalid glyph input");
                ultimate |= move.command == "Dl" &&
                            move.condition.find("Berserker") != std::string_view::npos;
            }
            check(ultimate, "Missing ultimate requirement");
        }
        check(!seed::fighter_moves(0) && !seed::fighter_moves(28), "Invalid native ID accepted");
        for (unsigned fighter = 1; fighter <= 27; ++fighter) {
            const auto cancels = seed::fighter_guard_cancels(fighter);
            check(cancels.size() == 4, "Missing guard cancels");
            for (const auto& move : cancels) {
                check(move.air == seed::MoveAir::Allowed, "Guard cancel air availability lost");
                for (const auto token : move.command)
                    check(std::string_view("UDBFablr").find(token) != std::string_view::npos,
                          "Invalid guard cancel glyph");
            }
        }
        check(seed::fighter_guard_cancels(0).empty() && seed::fighter_guard_cancels(28).empty(),
              "Invalid fighter has guard cancels");
        check(seed::fighter_moves(26)->berserker == "Newtype Mode" &&
                  seed::fighter_moves(27)->berserker == "SEED Mode",
              "Hidden variant type lost");
        check(seed::fighter_moves(24)->pilot == "Dearka Elsman" &&
                  seed::fighter_moves(25)->pilot == "Rey Za Burrel",
              "Zaku variants mixed");
        seed::set_movelist_fighters({25, 27}, true);
        check(seed::movelist_japanese() &&
                  seed::movelist_fighters() == std::array<std::uint8_t, 2>{25, 27},
              "Player selection metadata lost");
        seed::set_movelist_fighters({14, 25}, true);
        check(seed::movelist_default_fighter() == 14 &&
                  seed::fighter_moves(14)->name == "Sword Impulse",
              "Training did not default to the chosen Sword Impulse");
        seed::set_movelist_fighters({25, 14}, true, 1);
        check(seed::movelist_default_fighter() == 14,
              "Online joiner defaulted to the host fighter");
        std::puts("PASS: 27 native IDs, hidden variants, valid glyph commands, air flags and "
                  "Berserker requirements.");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
