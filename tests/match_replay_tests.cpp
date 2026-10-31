#include "match_replay.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass an E: scratch directory.");
        const std::filesystem::path root = std::filesystem::u8path(argv[1]);
        seed::MatchReplay replay;
        replay.recorded_at = 1793364000;
        replay.online = replay.complete = true;
        replay.combat_hash = std::string(64, 'a');
        replay.bios_hash = std::string(64, 'b');
        replay.fighters = {24, 27};
        replay.colors = {1, 0};
        replay.stage = 24;
        replay.names = {"Kira", "Athrun"};
        replay.initial_crc = 123;
        replay.final_crc = 456;
        replay.palettes[0].enabled = true;
        for (unsigned i = 0; i < 48; ++i)
            replay.palettes[0].colors[i] = std::uint16_t(i * 53);
        for (unsigned i = 0; i < 300; ++i)
            replay.inputs.push_back({std::uint16_t(i & 1023), std::uint16_t((i * 7) & 1023)});
        const auto file = seed::save_match_replay(root, replay);
        const auto before = seed::replay_file_hash(file);
        const auto restored = seed::read_match_replay(file);
        check(restored.inputs == replay.inputs && restored.palettes == replay.palettes &&
                  restored.fighters == replay.fighters && restored.names == replay.names &&
                  restored.stage == 24 && restored.colors == replay.colors &&
                  restored.final_crc == 456 && restored.initial_crc == 123 && restored.complete &&
                  restored.online,
              "Match details did not round-trip.");
        const auto second = seed::save_match_replay(root, replay);
        check(file != second && seed::replay_file_hash(file) == before,
              "Existing replay was overwritten.");
        std::ifstream in(file, std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
        unsigned rejected = 0;
        auto reject = [&](std::vector<char> broken, bool repair_crc) {
            if (repair_crc && broken.size() >= 4) {
                const auto crc = SDL_crc32(0, broken.data(), broken.size() - 4);
                for (unsigned n = 0; n < 4; ++n)
                    broken[broken.size() - 4 + n] = char(crc >> (8 * n));
            }
            const auto bad = root / "malformed.seedreplay";
            std::ofstream output(bad, std::ios::binary);
            output.write(broken.data(), broken.size());
            output.close();
            bool failed = false;
            try {
                seed::read_match_replay(bad);
            } catch (const std::exception&) {
                failed = true;
            }
            check(failed, "Malformed replay was accepted.");
            ++rejected;
        };
        for (std::size_t size : {0ull, 12ull, std::size_t(bytes.size() - 1)})
            reject(std::vector<char>(bytes.begin(), bytes.begin() + size), false);
        auto bad = bytes;
        bad[0] ^= 0x10;
        reject(bad, true);
        bad = bytes;
        bad[20] = 2;
        reject(bad, true); // Non-boolean online flag.
        bad = bytes;
        const std::string hash_marker(64, 'a');
        const auto hash =
            std::search(bad.begin(), bad.end(), hash_marker.begin(), hash_marker.end());
        check(hash != bad.end(), "Fixture is missing its hash.");
        *hash = 'z';
        reject(bad, true); // Invalid build hash.
        bad = bytes;
        bad[bad.size() - 6] = char(0xff);
        bad[bad.size() - 5] = char(0xff);
        reject(bad, true);
        bad = bytes;
        bad[100] ^= 1;
        reject(bad, false);
        bad = bytes;
        bad.push_back(0);
        reject(bad, true);
        replay.complete = false;
        replay.final_crc = 0;
        check(!seed::read_match_replay(seed::save_match_replay(root, replay)).complete,
              "Partial recording was mislabelled.");
        replay.fighters[0] = 0;
        bool failed = false;
        try {
            seed::save_match_replay(root, replay);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid fighter was written.");
        std::cout << "PASS: input/palette/metadata round-trip, unique files, partial matches, "
                  << rejected << " malformed replay cases, invalid fighter rejection.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
