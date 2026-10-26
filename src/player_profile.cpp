#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "player_profile.h"
#include <cstdio>
#include <fstream>
#include <iterator>
namespace seed {
std::string normalize_player_name(const std::string& input) {
    std::string value;
    for (unsigned char c : input)
        if (c >= 32 && c != 127)
            value += char(c);
    const auto start = value.find_first_not_of(' '), end = value.find_last_not_of(' ');
    if (start == std::string::npos)
        return "Player";
    value = value.substr(start, end - start + 1);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()),
                             nullptr, 0))
        return "Player";
    if (value.size() > kPlayerNameBytes) {
        auto n = kPlayerNameBytes;
        while (n && (static_cast<unsigned char>(value[n]) & 0xc0) == 0x80)
            --n;
        value.resize(n);
    }
    return value.empty() ? "Player" : value;
}
bool valid_player_name(const std::string& value) {
    return !value.empty() && value.size() <= kPlayerNameBytes &&
           normalize_player_name(value) == value;
}
std::string load_player_name(const std::filesystem::path& folder) {
    std::ifstream file(folder / "player-name.txt", std::ios::binary);
    char b[256]{};
    file.read(b, sizeof(b));
    return normalize_player_name(std::string(b, std::size_t(file.gcount())));
}
bool save_player_name(const std::filesystem::path& folder, const std::string& name,
                      std::string& error) {
    const auto target = folder / "player-name.txt", temp = folder / "player-name.txt.tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << normalize_player_name(name);
        file.close();
        if (!file) {
            error = "Could not save your player name in this folder.";
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Could not save your player name in this folder.";
        return false;
    }
    return true;
}
std::string online_match_title(const std::array<std::string, 2>& players, unsigned delay,
                               unsigned rollback, float ping) {
    char tail[160];
    std::snprintf(tail, sizeof(tail), " | Delay %uf | Rollback %uf | Ping %.0fms", delay, rollback,
                  ping);
    return players[0] + " vs " + players[1] + tail;
}
} // namespace seed
