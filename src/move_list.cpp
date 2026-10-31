#include "move_list.h"
namespace seed {
namespace {
using A = MoveAir;
// Native JP shared guard routines, reached by all 27 fighter updates:
// 0x080060C4 -> 0x08008B1C (R + direction), 0x08008D1C (back + A/B).
// Attack animations are selected from the current suit's own animation table.
constexpr MoveInput guard_cancels[]{{"Br", A::Allowed, "Escape back"},
                                    {"Fr", A::Allowed, "Escape forward"},
                                    {"Ba", A::Allowed, "Counter A"},
                                    {"Bb", A::Allowed, "Counter B"}};
// Functional command/availability facts from kira_yamato_88's guide v1.68.
// No guide descriptions, ratings, strategies, or narrative are reproduced.
constexpr MoveInput strike[]{
    {"Fa", A::Allowed}, {"l"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput aegis[]{
    {"Fa", A::Allowed}, {"Fl", A::Allowed}, {"Bl"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput buster[]{{"Fa", A::Allowed}, {"Ba", A::Allowed},
                             {"Da", A::Only},    {"l"},
                             {"l", A::Only},     {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput duel[]{
    {"Fa"}, {"Da", A::Only}, {"l", A::Allowed}, {"Fl"}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput blitz[]{
    {"a"}, {"Fa", A::Allowed}, {"Ua"}, {"l", A::Allowed}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput cgue[]{{"Fa", A::Allowed}, {"l", A::Allowed}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput calamity[]{
    {"a", A::Allowed}, {"Fa", A::Allowed}, {"l"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput forbidden[]{
    {"Fa", A::Allowed}, {"l", A::Allowed}, {"Fl", A::Allowed}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput raider[]{
    {"a", A::Allowed}, {"l"}, {"Fl"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput freedom[]{
    {"Fa", A::Allowed}, {"l"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput justice[]{
    {"Fa"}, {"Fa", A::Only}, {"l", A::Allowed}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput providence[]{
    {"a", A::Allowed}, {"Fa", A::Allowed}, {"l", A::Allowed}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput force[]{{"Fa", A::Allowed},
                            {"l"},
                            {"Fl", A::Allowed},
                            {"Fl>b", A::Allowed, "Follow-up on hit"},
                            {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput sword[]{
    {"Fa", A::Allowed}, {"l"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput blast[]{
    {"Fa", A::Allowed}, {"Da"}, {"l"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput chaos[]{{"Fa", A::Allowed},
                            {"Da", A::Only},
                            {"l"},
                            {"l", A::Only},
                            {"DFl", A::Only},
                            {"BDl", A::Only},
                            {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput gaia[]{{"Fa", A::Ground, "MS"},
                           {"l", A::Allowed, "MS"},
                           {"Dl", A::Ground, "MS / Berserker"},
                           {"Fa", A::Ground, "MA"},
                           {"Fb", A::Allowed, "MA"},
                           {"l", A::Ground, "MA"},
                           {"Dl", A::Ground, "MA / Berserker"}};
constexpr MoveInput abyss[]{
    {"Fa", A::Allowed}, {"Da"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput savior[]{{"Fa"}, {"l"},          {"Fl"},
                             {"Bl"}, {"l", A::Only}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput gold[]{{"a", A::Allowed},
                           {"Fa", A::Allowed},
                           {"Da", A::Allowed},
                           {"l", A::Allowed},
                           {"Fl", A::Allowed},
                           {"Dl", A::Only},
                           {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput red[]{{"Fl"},
                          {"l"},
                          {"Bl"},
                          {"Dl", A::Ground, "Berserker"},
                          {"Dl>a", A::Ground, "Berserker follow-up"}};
constexpr MoveInput slash[]{{"a", A::Allowed},
                            {"l", A::Allowed},
                            {"Fl"},
                            {"Fl>b", A::Ground, "Follow-up"},
                            {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput luna[]{{"a", A::Allowed}, {"l", A::Allowed}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput dearka[]{
    {"a", A::Allowed}, {"l"}, {"Fl"}, {"Bl"}, {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput rey[]{{"a", A::Allowed},
                          {"Da", A::Allowed},
                          {"l", A::Allowed},
                          {"Dl", A::Only},
                          {"Dl", A::Ground, "Berserker"}};
constexpr MoveInput mwu[]{{"Fa", A::Allowed},
                          {"l"},
                          {"l", A::Only},
                          {"Dl", A::Ground, "Berserker"},
                          {"Dl>a", A::Ground, "Berserker follow-up"}};
constexpr MoveInput rouge[]{
    {"Fa", A::Allowed}, {"l", A::Allowed}, {"Fl"}, {"Dl", A::Ground, "Berserker"}};
constexpr FighterMoves roster[]{
    {1, "Strike", "Kira Yamato", "SEED Mode", strike},
    {2, "Aegis", "Athrun Zala", "SEED Mode", aegis},
    {3, "Buster", "Dearka Elsman", "Normal Berserk", buster},
    {4, "Duel Assault Shroud", "Yzak Joule", "Normal Berserk", duel},
    {5, "Blitz", "Nicol Amalfi", "Normal Berserk", blitz},
    {6, "CGUE", "Rau Le Creuset", "Newtype Mode", cgue},
    {7, "Calamity", "Orga Sabnak", "Normal Berserk", calamity},
    {8, "Forbidden", "Shani Andras", "Normal Berserk", forbidden},
    {9, "Raider", "Clotho Buer", "Normal Berserk", raider},
    {10, "Freedom", "Kira Yamato", "SEED Mode", freedom},
    {11, "Justice", "Athrun Zala", "SEED Mode", justice},
    {12, "Providence", "Rau Le Creuset", "Newtype Mode", providence},
    {13, "Force Impulse", "Shinn Asuka", "Normal Berserk", force},
    {14, "Sword Impulse", "Shinn Asuka", "Normal Berserk", sword},
    {15, "Blast Impulse", "Shinn Asuka", "Normal Berserk", blast},
    {16, "Chaos", "Sting Oakley", "Normal Berserk", chaos},
    {17, "Gaia", "Stella Loussier", "Normal Berserk", gaia},
    {18, "Abyss", "Auel Neider", "Normal Berserk", abyss},
    {19, "Savior", "Athrun Zala", "SEED Mode", savior},
    {20, "Astray Gold Frame Amatsu Mina", "Rondo Mina Sahaku", "Normal Berserk", gold},
    {21, "Astray Red Frame", "Lowe Guele", "Normal Berserk", red},
    {22, "Slash Zaku Phantom", "Yzak Joule", "Normal Berserk", slash},
    {23, "Gunner Zaku Warrior", "Lunamaria Hawke", "Normal Berserk", luna},
    {24, "Gunner Zaku Warrior", "Dearka Elsman", "Normal Berserk", dearka},
    {25, "Blaze Zaku Phantom", "Rey Za Burrel", "Newtype Mode", rey},
    {26, "Strike", "Mu La Flaga", "Newtype Mode", mwu},
    {27, "Strike Rouge", "Cagalli Yula Athha", "SEED Mode", rouge}};
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
