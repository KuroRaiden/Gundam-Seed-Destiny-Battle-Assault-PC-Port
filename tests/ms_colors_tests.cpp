#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ms_color_profiles.h"
#include "ms_color_preview.h"
#include "ms_palette_fixture.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        const auto folder = std::filesystem::temp_directory_path() /
                            ("seed-ms-colors-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(folder);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                for (const auto name : {"ms-colors.ini", "ms-colors.ini.pending", "bad.ini"})
                    std::filesystem::remove(path / name, error);
                std::filesystem::remove(path, error);
            }
        } cleanup{folder};
        seed::MsColorProfiles profiles, restored;
        std::string error;
        require(profiles.set(0, 27, 1, ms_test_palette(0)) &&
                    profiles.set(1, 8, 0, ms_test_palette(1)),
                "Cannot store valid palettes");
        require(!profiles.set(2, 1, 0, ms_test_palette(0)) &&
                    !profiles.set(0, 28, 0, ms_test_palette(0)),
                "Invalid profile key accepted");
        require(profiles.save(folder / "ms-colors.ini", error), error.c_str());
        require(restored.load(folder / "ms-colors.ini", error) && restored == profiles,
                "Saved color profile did not round trip");
        auto invalid = ms_test_palette(0);
        invalid.colors[12] = 0xffff;
        require(!profiles.set(0, 1, 0, invalid), "Non-GBA color accepted");
        std::array<std::uint8_t, seed::kMsPaletteBytes> encoded{};
        auto value = ms_test_palette(1);
        seed::encode_palette(encoded.data(), value);
        seed::CustomPalette decoded;
        require(seed::decode_palette(encoded.data(), 1, decoded) && decoded == value,
                "Palette wire codec changed colors");
        require(!seed::decode_palette(encoded.data(), 0, decoded) &&
                    !seed::decode_palette(encoded.data(), 2, decoded),
                "Noncanonical palette flags accepted");
        for (const auto text :
             {"bad header\n", "SEEDPC_MS_COLORS 1\n0 1 0 ffff\n", "SEEDPC_MS_COLORS 1\n3 1 0\n"}) {
            std::ofstream(folder / "bad.ini") << text;
            require(!restored.load(folder / "bad.ini", error) && !restored.get(0, 27, 1).enabled,
                    "Malformed profiles partially applied");
        }
        require(profiles.set(0, 27, 1, {}) && !profiles.get(0, 27, 1).enabled,
                "Reset to original did not clear the override");
        if (argc > 1) {
            const auto previews = seed::load_ms_previews(std::filesystem::path(argv[1]));
            require(previews.size() == 27, "Missing native preview variants");
            for (const auto& [id, preview] : previews) {
                auto colors = preview.originals[0];
                const auto before = preview.rgba(colors);
                unsigned used = 0;
                for (auto i : preview.indices)
                    if (i != 255) {
                        used = i;
                        break;
                    }
                colors[used] ^= 0x7fff;
                const auto after = preview.rgba(colors);
                bool changed = false;
                for (unsigned n = 0; n < preview.indices.size(); ++n)
                    for (unsigned ch = 0; ch < 4; ++ch) {
                        if (preview.indices[n] != used)
                            require(before[n * 4 + ch] == after[n * 4 + ch],
                                    "Preview recolored an unrelated swatch/background pixel");
                        changed |= before[n * 4 + ch] != after[n * 4 + ch];
                    }
                require(changed, "Preview swatch edit had no visible effect");
            }
        }
        std::puts("PASS: MS profile persistence/reset, canonical palette wire format, malformed "
                  "data rejection and indexed preview recoloring.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
