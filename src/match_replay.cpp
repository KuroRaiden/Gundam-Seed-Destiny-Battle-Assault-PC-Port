#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <SDL3/SDL.h>
#include "match_replay.h"
#include "jp_stages.h"
#include "player_profile.h"
#include "portable_paths.h"
#include <algorithm>
#include <fstream>
#include <span>
#include <stdexcept>

namespace seed {
namespace {
constexpr std::uint32_t kSchema = 1;
constexpr std::size_t kMaxBytes = kMaxReplayFrames * 4ull + 4096;
void check(bool ok, const char* error) {
    if (!ok)
        throw std::runtime_error(error);
}
bool hash_valid(const std::string& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
void validate(const MatchReplay& replay) {
    check(!replay.build_version.empty() && replay.build_version.size() <= 64 &&
              std::all_of(replay.build_version.begin(), replay.build_version.end(),
                          [](char c) {
                              return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.' ||
                                     c == '-';
                          }),
          "Replay has an invalid game version.");
    check(hash_valid(replay.combat_hash) && hash_valid(replay.bios_hash),
          "Replay has invalid compatibility information.");
    check(replay.recorded_at && valid_jp_stage(replay.stage), "Replay has invalid match details.");
    check(!replay.inputs.empty() && replay.inputs.size() <= kMaxReplayFrames,
          "Replay has an invalid length.");
    check(replay.complete || replay.final_crc == 0, "Replay has invalid completion information.");
    const auto& t = replay.initial_timing;
    check(t[0] < (1ull << 56) && t[1] < 10'000'000'000ull && t[2] <= t[1] && t[3] <= 280896 &&
              std::int64_t(t[4]) >= -280896 && std::int64_t(t[4]) <= 280896,
          "Replay has invalid startup timing.");
    for (unsigned p = 0; p < 2; ++p) {
        check(replay.fighters[p] >= 1 && replay.fighters[p] <= 27 && replay.colors[p] <= 1 &&
                  replay.palettes[p].valid() && valid_player_name(replay.names[p]),
              "Replay has invalid player information.");
    }
    for (const auto& frame : replay.inputs)
        check(!(frame[0] & ~1023u) && !(frame[1] & ~1023u), "Replay has invalid button inputs.");
}
struct Writer {
    std::vector<std::uint8_t> data;
    void number(std::uint64_t n, unsigned size) {
        for (unsigned i = 0; i < size; ++i)
            data.push_back(std::uint8_t(n >> (i * 8)));
    }
    void text(const std::string& value) {
        number(value.size(), 2);
        data.insert(data.end(), value.begin(), value.end());
    }
};
struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t at = 0;
    std::uint64_t number(unsigned size) {
        check(at + size <= data.size(), "Replay file is truncated.");
        std::uint64_t n = 0;
        for (unsigned i = 0; i < size; ++i)
            n |= std::uint64_t(data[at++]) << (i * 8);
        return n;
    }
    bool flag() {
        const auto n = number(1);
        check(n <= 1, "Replay has an invalid flag.");
        return n != 0;
    }
    std::string text(unsigned limit) {
        const auto size = number(2);
        check(size <= limit && at + size <= data.size(), "Replay has invalid text.");
        std::string out(reinterpret_cast<const char*>(data.data() + at), std::size_t(size));
        at += size;
        return out;
    }
};
std::string hex(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
} // namespace
const char* replay_build_version() {
    return SEED_REPLAY_BUILD_VERSION;
}
std::string replay_file_hash(const std::filesystem::path& file) {
    struct Handles {
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ~Handles() {
            if (hash)
                BCryptDestroyHash(hash);
            if (alg)
                BCryptCloseAlgorithmProvider(alg, 0);
        }
    } handles;
    check(BCryptOpenAlgorithmProvider(&handles.alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptCreateHash(handles.alg, &handles.hash, nullptr, 0, nullptr, 0, 0) >= 0,
          "Cannot initialize replay compatibility checks.");
    std::ifstream input(file, std::ios::binary);
    check(bool(input), "Cannot read a replay compatibility file.");
    std::array<unsigned char, 65536> bytes;
    while (input) {
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        check(BCryptHashData(handles.hash, bytes.data(), ULONG(input.gcount()), 0) >= 0,
              "Replay compatibility check failed.");
    }
    check(input.eof(), "Cannot finish replay compatibility check.");
    std::array<unsigned char, 32> digest;
    check(BCryptFinishHash(handles.hash, digest.data(), ULONG(digest.size()), 0) >= 0,
          "Replay compatibility check failed.");
    return hex(digest);
}
std::string replay_unique_id() {
    std::array<unsigned char, 16> bytes;
    check(BCryptGenRandom(nullptr, bytes.data(), ULONG(bytes.size()),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
          "Cannot allocate a unique replay filename.");
    return hex(bytes);
}
std::filesystem::path combat_library_path() {
    // This function is linked into the launcher, host and combat DLL. Each
    // locates the same adjacent library without consulting the working folder.
    HMODULE module = nullptr;
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&combat_library_path), &module) != 0,
          "Cannot find the combat library.");
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    check(length && length < path.size(), "Cannot find the combat library.");
    path.resize(length);
    return std::filesystem::path(path).parent_path() / "SeedJpCombat.dll";
}
MatchReplay read_match_replay(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    check(bool(input), "Cannot open this replay.");
    const auto size = input.tellg();
    check(size >= 32 && size <= kMaxBytes, "Replay file has an invalid size.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    check(bool(input), "Cannot read this replay completely.");
    Reader footer{std::span(bytes).last(4)};
    check(SDL_crc32(0, bytes.data(), bytes.size() - 4) == footer.number(4),
          "Replay is damaged (file checksum mismatch).");
    Reader reader{std::span(bytes).first(bytes.size() - 4)};
    check(reader.number(8) == 0x314c505244454553ull && reader.number(4) == kSchema,
          "Unsupported replay format."); // SEEDRPL1, explicit little-endian.
    MatchReplay replay;
    replay.recorded_at = reader.number(8);
    replay.online = reader.flag();
    replay.complete = reader.flag();
    replay.build_version = reader.text(64);
    replay.combat_hash = reader.text(64);
    replay.bios_hash = reader.text(64);
    replay.initial_crc = std::uint32_t(reader.number(4));
    for (auto& value : replay.initial_timing)
        value = reader.number(8);
    replay.final_crc = std::uint32_t(reader.number(4));
    replay.stage = std::uint8_t(reader.number(1));
    for (unsigned p = 0; p < 2; ++p) {
        replay.fighters[p] = std::uint8_t(reader.number(1));
        replay.colors[p] = std::uint8_t(reader.number(1));
        replay.names[p] = reader.text(kPlayerNameBytes);
        replay.palettes[p].enabled = reader.flag();
        for (auto& color : replay.palettes[p].colors)
            color = std::uint16_t(reader.number(2));
    }
    const auto frames = reader.number(4);
    check(frames && frames <= kMaxReplayFrames && reader.data.size() - reader.at == frames * 4,
          "Replay has an invalid input count.");
    replay.inputs.resize(std::size_t(frames));
    for (auto& frame : replay.inputs)
        for (auto& input : frame)
            input = std::uint16_t(reader.number(2));
    validate(replay);
    return replay;
}
std::filesystem::path save_match_replay(const std::filesystem::path& folder,
                                        const MatchReplay& replay) {
    validate(replay);
    require_plain_folder(folder);
    Writer writer;
    writer.number(0x314c505244454553ull, 8);
    writer.number(kSchema, 4);
    writer.number(replay.recorded_at, 8);
    writer.number(replay.online, 1);
    writer.number(replay.complete, 1);
    writer.text(replay.build_version);
    writer.text(replay.combat_hash);
    writer.text(replay.bios_hash);
    writer.number(replay.initial_crc, 4);
    for (auto value : replay.initial_timing)
        writer.number(value, 8);
    writer.number(replay.final_crc, 4);
    writer.number(replay.stage, 1);
    for (unsigned p = 0; p < 2; ++p) {
        writer.number(replay.fighters[p], 1);
        writer.number(replay.colors[p], 1);
        writer.text(replay.names[p]);
        writer.number(replay.palettes[p].enabled, 1);
        for (auto color : replay.palettes[p].colors)
            writer.number(color, 2);
    }
    writer.number(replay.inputs.size(), 4);
    for (const auto& frame : replay.inputs)
        for (auto input : frame)
            writer.number(input, 2);
    const auto crc = SDL_crc32(0, writer.data.data(), writer.data.size());
    writer.number(crc, 4);
    const auto file =
        folder / (std::to_string(replay.recorded_at) + "-" + replay_unique_id() + ".seedreplay");
    auto pending = file;
    pending += ".pending";
    try {
        std::ofstream output(pending, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(writer.data.data()), writer.data.size());
        output.close();
        check(bool(output), "Could not write the replay. Check free disk space.");
        std::filesystem::rename(pending, file);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        throw;
    }
    return file;
}
} // namespace seed
