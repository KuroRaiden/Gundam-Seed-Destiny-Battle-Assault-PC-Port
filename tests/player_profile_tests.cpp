#include "player_profile.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        check(seed::normalize_player_name("  Kira\r\n ") == "Kira", "trim/control characters");
        check(!seed::valid_player_name(std::string("A\0B", 3)), "embedded null accepted");
        check(!seed::valid_player_name("\xF0\x28\x8C\x28"), "invalid UTF8 accepted");
        check(seed::normalize_player_name("") == "Player", "empty default");
        std::string japanese = "キラ・ヤマト";
        check(seed::valid_player_name(japanese), "Unicode name rejected");
        std::string long_name;
        for (int i = 0; i < 30; ++i)
            long_name += "キ";
        auto bounded = seed::normalize_player_name(long_name);
        check(bounded.size() == 48 && seed::valid_player_name(bounded),
              "UTF8 truncation split codepoint");
        const auto folder =
            std::filesystem::temp_directory_path() /
            ("seed-profile-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(folder);
        std::string error;
        check(seed::save_player_name(folder, japanese, error), "save Unicode profile");
        check(seed::load_player_name(folder) == japanese, "profile relocation load");
        check(seed::save_player_name(folder, "Athrun", error) &&
                  seed::load_player_name(folder) == "Athrun",
              "atomic profile replacement");
        std::filesystem::remove(folder / "config/player-name.txt");
        std::filesystem::remove(folder / "config");
        std::filesystem::remove(folder);
        check(seed::online_match_title({"Kira", "Athrun"}, 2, 7, 83.4f) ==
                  "Kira vs Athrun | Delay 2f | Rollback 7f | Ping 83ms",
              "title statistics");
        std::puts("PASS: Unicode player names, invalid input rejection, bounded UTF8, portable "
                  "persistence and title statistics.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
