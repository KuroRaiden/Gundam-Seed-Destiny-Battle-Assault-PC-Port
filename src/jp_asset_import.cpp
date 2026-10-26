#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "jp_asset_import.h"
#include "game_addresses.h"
#include "runtime.h"
#include "runtime_arm.h"
#include "frame_control.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace common = seed::address::common;
namespace jp = seed::address::jp;
using Bytes = std::vector<std::uint8_t>;
constexpr unsigned width = 240, height = 160;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Bytes read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot read an input file");
    return Bytes(std::istreambuf_iterator<char>(input), {});
}
std::string hash(const Bytes& data, BCRYPT_ALG_HANDLE algorithm, unsigned length) {
    Bytes digest(length);
    require(BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data.data()), ULONG(data.size()),
                       digest.data(), ULONG(digest.size())) >= 0,
            "Cannot verify game files");
    const char* hex = "0123456789abcdef";
    std::string result;
    for (auto value : digest) {
        result += hex[value >> 4];
        result += hex[value & 15];
    }
    return result;
}
unsigned u16(const Bytes& bytes, std::size_t at) {
    require(at + 2 <= bytes.size(), "Truncated donor resource");
    return bytes[at] | (unsigned(bytes[at + 1]) << 8);
}
unsigned u32(const Bytes& bytes, std::size_t at) {
    require(at + 4 <= bytes.size(), "Truncated donor resource");
    return u16(bytes, at) | (u16(bytes, at + 2) << 16);
}
void put16(Bytes& out, unsigned value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}
void put32(Bytes& out, unsigned value) {
    put16(out, value);
    put16(out, value >> 16);
}
struct Image {
    unsigned id, width, height;
    std::vector<std::uint16_t> pixels;
};
class Donor {
  public:
    Bytes rom;
    std::map<std::pair<unsigned, unsigned>, Bytes> resources;
    explicit Donor(const std::filesystem::path& path) : rom(read_file(path)) {
        require(rom.size() == 8 * 1024 * 1024 &&
                    hash(rom, BCRYPT_SHA256_ALG_HANDLE, 32) ==
                        "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb",
                "Select the supported Japanese Destiny cartridge");
    }
    unsigned rom32(unsigned address) const {
        require(address >= common::kRomBegin, "Invalid donor pointer");
        return u32(rom, address - common::kRomBegin);
    }
    const Bytes& resource(unsigned group, unsigned index) {
        const auto key = std::pair{group, index};
        if (auto found = resources.find(key); found != resources.end())
            return found->second;
        const std::size_t archive = jp::kResourceArchive - common::kRomBegin;
        const auto table = archive + u32(rom, archive + group * 4),
                   end = archive + u32(rom, archive + (group + 1) * 4);
        const auto header = u16(rom, table);
        require(header % 2 == 0 && index + 1 < header / 2, "Invalid donor resource index");
        const auto begin = table + u16(rom, table + index * 2);
        auto next = table + u16(rom, table + (index + 1) * 2);
        require(begin >= table + header && begin < end && next >= begin && next <= end &&
                    end <= rom.size(),
                "Invalid donor resource bounds");
        Bytes result;
        if (next - begin >= 6 && rom[begin] == 'L' && rom[begin + 1] == 'e') {
            const auto size = unsigned(rom[begin + 2]) | (unsigned(rom[begin + 3]) << 8) |
                              (unsigned(rom[begin + 4]) << 16);
            require(size && size <= 16 * 1024 * 1024, "Invalid donor decompression size");
            std::size_t cursor = begin + 6;
            auto byte = [&] {
                require(cursor < next, "Truncated compressed donor resource");
                return rom[cursor++];
            };
            while (result.size() < size) {
                auto flags = byte();
                for (unsigned token = 0; token < 4 && result.size() < size; ++token, flags >>= 2) {
                    const auto kind = flags & 3;
                    if (kind >= 2) {
                        const auto count = kind == 2 ? 1 : 3;
                        for (int n = 0; n < count; ++n)
                            result.push_back(byte());
                    } else {
                        unsigned distance, length;
                        if (kind == 1) {
                            const auto value = byte();
                            distance = (value & 3) + 1;
                            length = (value >> 2) + 2;
                        } else {
                            const auto low = byte();
                            const auto value = unsigned(low) | (unsigned(byte()) << 8);
                            distance = (value & 0xfff) + 5;
                            length = (value >> 12) + 3;
                        }
                        require(distance <= result.size(), "Invalid donor back reference");
                        while (length--)
                            result.push_back(result[result.size() - distance]);
                    }
                    require(result.size() <= size,
                            "Donor decompression exceeded its declared size");
                }
            }
        } else
            result.assign(rom.begin() + begin, rom.begin() + next);
        return resources.emplace(key, std::move(result)).first->second;
    }
    unsigned tile_pixel(const Bytes& data, unsigned tile, unsigned x, unsigned y) const {
        const auto offset = std::size_t(tile) * 32 + y * 4 + x / 2;
        require(offset < data.size(), "Donor sprite exceeds tile storage");
        return (data[offset] >> (4 * (x & 1))) & 15;
    }
    Image sprite(unsigned id, unsigned frame, const Bytes& tiles, const Bytes& palette) {
        const auto table = rom32(jp::kSpriteBankTable + 4 * (199 + 1));
        const auto at = rom32(table + 4 * frame) - common::kRomBegin;
        const auto count = u16(rom, at);
        require(count && count <= 128, "Invalid donor sprite part count");
        static constexpr unsigned sizes[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                                                    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                                                    {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
        struct Part {
            unsigned tile, w, h;
            int x, y;
            bool flip_x, flip_y;
        };
        std::vector<Part> parts;
        int left = 10000, top = 10000, right = -10000, bottom = -10000;
        for (unsigned n = 0; n < count; ++n) {
            const auto offset = at + 4 + n * 8;
            require(offset + 8 <= rom.size(), "Truncated donor sprite");
            const auto a0 = rom[offset], a1 = rom[offset + 1];
            const auto shape = a0 >> 6, size = a1 >> 6;
            require(shape < 3, "Invalid donor sprite shape");
            Part part{u16(rom, offset + 2) & 1023,
                      sizes[shape][size][0],
                      sizes[shape][size][1],
                      std::int16_t(u16(rom, offset + 4)),
                      std::int16_t(u16(rom, offset + 6)),
                      bool(a1 & 16),
                      bool(a1 & 32)};
            left = std::min(left, part.x);
            top = std::min(top, part.y);
            right = std::max(right, part.x + int(part.w));
            bottom = std::max(bottom, part.y + int(part.h));
            parts.push_back(part);
        }
        require(right > left && bottom > top && right - left <= 240 && bottom - top <= 160,
                "Invalid donor sprite extent");
        Image image{id, unsigned(right - left), unsigned(bottom - top),
                    std::vector<std::uint16_t>(std::size_t(right - left) * (bottom - top), 0x8000)};
        for (auto part = parts.rbegin(); part != parts.rend(); ++part)
            for (unsigned y = 0; y < part->h; ++y)
                for (unsigned x = 0; x < part->w; ++x) {
                    const auto tx = part->flip_x ? part->w - 1 - x : x,
                               ty = part->flip_y ? part->h - 1 - y : y;
                    const auto color = tile_pixel(
                        tiles, part->tile + (ty / 8) * (part->w / 8) + tx / 8, tx % 8, ty % 8);
                    if (color)
                        image.pixels[(part->y - top + y) * image.width + part->x - left + x] =
                            std::uint16_t(u16(palette, color * 2) & 0x7fff);
                }
        return image;
    }
    void write_pack(const Bytes& rgb, const std::filesystem::path& output) {
        require(rgb.size() == width * height * 3, "Unexpected selector capture dimensions");
        Image background{0, width, height, {}};
        background.pixels.reserve(width * height);
        for (std::size_t at = 0; at < rgb.size(); at += 3)
            background.pixels.push_back(std::uint16_t((rgb[at] >> 3) | ((rgb[at + 1] >> 3) << 5) |
                                                      ((rgb[at + 2] >> 3) << 10)));
        const auto& tiles = resource(19, 0);
        const auto& map = resource(19, 1);
        Bytes palette(64);
        const auto& colors = resource(19, 2);
        palette.insert(palette.end(), colors.begin(), colors.end());
        require(u32(map, 0) == 30 && u32(map, 4) == 20, "Unexpected portrait tilemap");
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto entry = u16(map, 16 + 2 * ((y / 8) * 30 + x / 8));
                const auto tx = entry & 1024 ? 7 - x % 8 : x % 8,
                           ty = entry & 2048 ? 7 - y % 8 : y % 8;
                const auto index = tile_pixel(tiles, entry & 1023, tx, ty);
                if (index)
                    background.pixels[y * width + x] =
                        std::uint16_t(u16(palette, (entry >> 12) * 32 + index * 2) & 0x7fff);
            }
        std::vector<Image> images;
        images.push_back(std::move(background));
        for (unsigned fighter = 0; fighter < 30; ++fighter) {
            const unsigned group = fighter < 20 ? 17 : 18,
                           index = fighter < 20 ? fighter : fighter - 20;
            images.push_back(sprite(100 + fighter, 0, resource(group, index),
                                    resource(group, index + (group == 17 ? 20 : 10))));
            images.push_back(
                sprite(200 + fighter, 2, resource(4, 22 + fighter), resource(4, 52 + fighter)));
            images.push_back(sprite(300 + fighter, 8, resource(4, 82 + fighter), resource(4, 15)));
        }
        Bytes pack{'S', 'E', 'E', 'D', 'J', 'P', '1', 0};
        put32(pack, width);
        put32(pack, height);
        put32(pack, unsigned(images.size()));
        const auto rom_hash = hash(rom, BCRYPT_SHA256_ALG_HANDLE, 32);
        for (std::size_t at = 0; at < rom_hash.size(); at += 2)
            pack.push_back(std::uint8_t(std::stoul(rom_hash.substr(at, 2), nullptr, 16)));
        const auto grid = jp::kPortraitGrid - common::kRomBegin;
        require(grid + 32 <= rom.size(), "Missing donor grid");
        pack.insert(pack.end(), rom.begin() + grid, rom.begin() + grid + 32);
        for (const auto& image : images) {
            put32(pack, image.id);
            put32(pack, image.width);
            put32(pack, image.height);
            for (auto pixel : image.pixels)
                put16(pack, pixel);
        }
        const auto pending =
            output.wstring() + L".pending-" + std::to_wstring(GetCurrentProcessId());
        {
            std::ofstream file(pending, std::ios::binary);
            file.write(reinterpret_cast<const char*>(pack.data()), std::streamsize(pack.size()));
            file.close();
            require(bool(file), "Cannot write local selector art");
        }
        if (!MoveFileExW(pending.c_str(), output.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(pending);
            throw std::runtime_error("Cannot install local selector art");
        }
        std::fprintf(stderr, "Imported JP selection art locally: %zu images, %zu bytes\n",
                     images.size(), pack.size());
    }
};
struct Capture {
    Donor* donor;
    std::filesystem::path output;
};
Capture* active_capture = nullptr;
int capture(gbarecomp::NativeFrameControl& machine) {
    auto advance = [&](unsigned count) {
        for (unsigned frame = 0; frame < count; ++frame)
            require(machine.advance(1023), "JP selector preparation stopped");
    };
    advance(120);
    bus_write_u32(jp::kGameState, jp::kStateCharacterSelect);
    bus_write_u32(jp::kSubstate, 0);
    bus_write_u32(jp::kStartupResetField, 0);
    bus_write_u8(jp::kSceneSetupByte, 1);
    bus_write_u8(jp::kMatchRuleMode, 6);
    bus_write_u8(jp::kSelectedFighter1, 1);
    bus_write_u8(jp::kSelectedFighter2, 8);
    for (unsigned n = 0; n < 13; ++n)
        bus_write_u8(jp::kSelectorTransientState + n, 0);
    advance(500);
    require(bus_read_u32(jp::kSubstate) == 8, "JP selector initialization did not finish");
    for (unsigned actor = 17; actor < 45; ++actor)
        if (actor < 21 || actor >= 33)
            bus_write_u16(jp::kSelectorActorRecords + actor * jp::kSelectorActorStride, 0);
    advance(2);
    const auto* pixels = machine.pixels();
    active_capture->donor->write_pack(Bytes(pixels, pixels + width * height * 3),
                                      active_capture->output);
    return 0;
}
} // namespace
extern "C" __declspec(dllexport) int SeedImportJpSelectionArt(const char* rom, const char* bios,
                                                              const char* destination) {
    try {
        require(rom && bios && destination, "Missing local game input paths");
        Donor donor(std::filesystem::u8path(rom));
        const auto firmware = read_file(std::filesystem::u8path(bios));
        require(firmware.size() == 16384 && hash(firmware, BCRYPT_SHA1_ALG_HANDLE, 20) ==
                                                "300c20df6731a33952ded8c436f7f186d25d3492",
                "Select the supported extracted GBA BIOS");
        const auto output = std::filesystem::absolute(std::filesystem::u8path(destination));
        std::error_code comparison_error;
        require(
            !std::filesystem::equivalent(output, std::filesystem::u8path(rom), comparison_error),
            "The asset cache must not overwrite your Japanese ROM");
        comparison_error.clear();
        require(
            !std::filesystem::equivalent(output, std::filesystem::u8path(bios), comparison_error),
            "The asset cache must not overwrite your BIOS");
        std::filesystem::create_directories(output.parent_path());
        const auto save = output.parent_path() /
                          (".asset-import-" + std::to_string(GetCurrentProcessId()) + ".sav");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove(path, error);
                active_capture = nullptr;
            }
        } cleanup{save};
        require(!std::filesystem::exists(save), "Selector import cache is already in use");
        Capture request{&donor, output};
        active_capture = &request;
        gbarecomp::RunOptions options;
        options.builtin_game_name = "Preparing local JP selection assets";
        options.builtin_rom_sha1 = "64eea52cd909669a972c091869c5f47865863bc0";
        options.diagnostic_replay_default = false;
        options.native_frame_driver = capture;
        const auto save_text = save.string();
        std::vector<std::string> values{"GundamSeedRecomp.exe", "--rom",  rom,      "--bios", bios,
                                        "--bios-hle",           "--save", save_text};
        std::vector<char*> args;
        for (auto& value : values)
            args.push_back(value.data());
        return gbarecomp::run_game(int(args.size()), args.data(), options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Local JP art import: %s\n", error.what());
        return 1;
    }
}
