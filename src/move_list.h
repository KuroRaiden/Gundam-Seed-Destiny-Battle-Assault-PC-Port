#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
namespace seed {
enum class MoveAir { Ground, Allowed, Only };
struct MoveInput {
    std::string_view command; // U/D/F/B directions; a/b/l/r are GBA buttons.
    MoveAir air = MoveAir::Ground;
    std::string_view condition;
};
struct FighterMoves {
    unsigned id;
    std::string_view name, pilot, berserker;
    std::span<const MoveInput> moves;
};
std::span<const FighterMoves> fighter_movelist();
const FighterMoves* fighter_moves(unsigned native_id);
// Presentation metadata only: never serialized into rollback snapshots.
void set_movelist_fighters(std::array<std::uint8_t, 2> fighters, bool japanese,
                           unsigned local_seat = 0);
std::array<std::uint8_t, 2> movelist_fighters();
bool movelist_japanese();
std::uint8_t movelist_default_fighter();
} // namespace seed
