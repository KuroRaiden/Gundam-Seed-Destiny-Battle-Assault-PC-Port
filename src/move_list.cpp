#include "move_list.h"
namespace seed {
namespace {
using A = MoveAir;
// Native JP shared guard routines, reached by all 27 fighter updates:
// 0x080060C4 -> 0x08008B1C (R + direction), 0x08008D1C (forward + A/B).
// Attack animations are selected from the current suit's own animation table.
constexpr MoveInput guard_cancels[]{{"Br", A::Allowed, "Escape back"},
                                    {"Fr", A::Allowed, "Escape forward"},
                                    {"Fa", A::Allowed, "Counter A"},
                                    {"Fb", A::Allowed, "Counter B"}};
#include "native_move_inputs.inc"
// Native portrait order, checked against the locally imported JP selection art.
constexpr FighterMoves roster[]{
    {1, "Strike", "Kira Yamato", "SEED Mode", native_1},
    {2, "Aegis", "Athrun Zala", "SEED Mode", native_2},
    {3, "Buster", "Dearka Elsman", "Normal Berserk", native_3},
    {4, "Raider", "Clotho Buer", "Normal Berserk", native_4},
    {5, "Calamity", "Orga Sabnak", "Normal Berserk", native_5},
    {6, "Forbidden", "Shani Andras", "Normal Berserk", native_6},
    {7, "Duel Assault Shroud", "Yzak Joule", "Normal Berserk", native_7},
    {8, "Blitz", "Nicol Amalfi", "Normal Berserk", native_8},
    {9, "CGUE", "Rau Le Creuset", "Newtype Mode", native_9},
    {10, "Astray Red Frame", "Lowe Guele", "Normal Berserk", native_10},
    {11, "Freedom", "Kira Yamato", "SEED Mode", native_11},
    {12, "Justice", "Athrun Zala", "SEED Mode", native_12},
    {13, "Force Impulse", "Shinn Asuka", "Normal Berserk", native_13},
    {14, "Sword Impulse", "Shinn Asuka", "Normal Berserk", native_14},
    {15, "Blast Impulse", "Shinn Asuka", "Normal Berserk", native_15},
    {16, "Chaos", "Sting Oakley", "Normal Berserk", native_16},
    {17, "Gaia", "Stella Loussier", "Normal Berserk", native_17},
    {18, "Abyss", "Auel Neider", "Normal Berserk", native_18},
    {19, "Savior", "Athrun Zala", "Normal Berserk", native_19},
    {20, "Slash Zaku Phantom", "Yzak Joule", "Normal Berserk", native_20},
    {21, "Providence", "Rau Le Creuset", "Newtype Mode", native_21},
    {22, "Astray Gold Frame Amatsu Mina", "Rondo Mina Sahaku", "Normal Berserk", native_22},
    {23, "Gunner Zaku Warrior", "Lunamaria Hawke", "Normal Berserk", native_23},
    {24, "Gunner Zaku Warrior", "Dearka Elsman", "Normal Berserk", native_24},
    {25, "Blaze Zaku Phantom", "Rey Za Burrel", "Newtype Mode", native_25},
    {26, "Strike", "Mu La Flaga", "Newtype Mode", native_26},
    {27, "Strike Rouge", "Cagalli Yula Athha", "SEED Mode", native_27},
};
std::array<std::uint8_t, 2> selected{1, 2};
bool japanese = false;
unsigned local_player = 0;
} // namespace
std::span<const FighterMoves> fighter_movelist() {
    return roster;
}
const FighterMoves* fighter_moves(unsigned id) {
    return id >= 1 && id <= 27 ? &roster[id - 1] : nullptr;
}
std::span<const MoveInput> fighter_guard_cancels(unsigned id) {
    return fighter_moves(id) ? std::span<const MoveInput>(guard_cancels)
                             : std::span<const MoveInput>{};
}
void set_movelist_fighters(std::array<std::uint8_t, 2> value, bool jp, unsigned local_seat) {
    selected = value;
    japanese = jp;
    local_player = local_seat < 2 ? local_seat : 0;
}
std::array<std::uint8_t, 2> movelist_fighters() {
    return selected;
}
std::uint8_t movelist_default_fighter() {
    const auto id = selected[local_player];
    return fighter_moves(id) ? id : 1;
}
bool movelist_japanese() {
    return japanese;
}
} // namespace seed
