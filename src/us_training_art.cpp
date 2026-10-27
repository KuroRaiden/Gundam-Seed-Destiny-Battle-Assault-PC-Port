#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "us_training_art.h"
#include "game_addresses.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
namespace seed {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr char magic[] = "SEEDUST1";
constexpr char identity[] = "d7952790a4b82b6d230a4953e4bfe48f6693ddd550f1cf0e7ace74bce95453f8";
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
Bytes read(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(bool(file), "Cannot read the US Training font input");
    const auto size = file.tellg();
    require(size >= 0 && std::uint64_t(size) <= limit, "US Training font input is too large");
    Bytes bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    require(bool(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())),
            "Truncated US Training font input");
    return bytes;
}
unsigned word(const Bytes& bytes, std::size_t at) {
    require(at + 2 <= bytes.size(), "Truncated US Training font data");
    return bytes[at] | (unsigned(bytes[at + 1]) << 8);
}
unsigned dword(const Bytes& bytes, std::size_t at) {
    return word(bytes, at) | (word(bytes, at + 2) << 16);
}
void put(Bytes& bytes, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(std::uint8_t(value >> (i * 8)));
}
Bytes decode(const Bytes& rom, std::size_t at) {
    require(at + 6 < rom.size() && rom[at] == 'L' && rom[at + 1] == 'e',
            "US Training font compression mismatch");
    const auto size =
        unsigned(rom[at + 2]) | (unsigned(rom[at + 3]) << 8) | (unsigned(rom[at + 4]) << 16);
    require(size == 7776, "US Training font resource size mismatch");
    at += 6;
    Bytes out;
    auto byte = [&] {
        require(at < rom.size(), "Truncated US font compression");
        return rom[at++];
    };
    while (out.size() < size) {
        auto flags = byte();
        for (unsigned n = 0; n < 4 && out.size() < size; ++n, flags >>= 2) {
            const auto kind = flags & 3;
            if (kind >= 2) {
                for (unsigned k = 0; k < (kind == 2 ? 1u : 3u); ++k)
                    out.push_back(byte());
            } else {
                unsigned distance, length;
                if (kind == 1) {
                    const auto b = byte();
                    distance = (b & 3) + 1;
                    length = (b >> 2) + 2;
                } else {
                    const auto lo = byte();
                    const auto v = lo | (unsigned(byte()) << 8);
                    distance = (v & 4095) + 5;
                    length = (v >> 12) + 3;
                }
                require(distance <= out.size(), "Invalid US font back reference");
                while (length--)
                    out.push_back(out[out.size() - distance]);
            }
            require(out.size() <= size, "US font decompression overflow");
        }
    }
    return out;
}
} // namespace
void UsTrainingArt::prepare(const std::filesystem::path& rom_path,
                            const std::filesystem::path& cache) {
    if (std::filesystem::is_regular_file(cache)) {
        try {
            UsTrainingArt check;
            check.load(cache);
            return;
        } catch (const std::exception&) {
        }
    }
    const auto rom = read(rom_path, 4 * 1024 * 1024);
    require(rom.size() == 4 * 1024 * 1024, "Select the supported US Battle Assault ROM");
    std::array<unsigned char, 32> digest{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(rom.data()),
                       ULONG(rom.size()), digest.data(), ULONG(digest.size())) >= 0,
            "Cannot verify US Training font donor");
    std::string hex;
    for (auto b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    require(hex == identity, "US Training font requires the supported Battle Assault ROM");
    const auto tiles = decode(rom, address::us::kTrainingMenuTiles - address::common::kRomBegin);
    constexpr unsigned sizes[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                                         {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                                         {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
    Bytes out(magic, magic + 8);
    out.insert(out.end(), identity, identity + 64);
    put(out, 19);
    for (unsigned frame = 0; frame < 19; ++frame) {
        const auto pointer =
            dword(rom, address::us::kTrainingMenuFrames - address::common::kRomBegin + frame * 4);
        require(pointer >= address::common::kRomBegin, "Invalid US font frame pointer");
        const auto at = pointer - address::common::kRomBegin, count = word(rom, at);
        require(count && count < 128, "Invalid US font frame size");
        struct Part {
            unsigned tile, w, h;
            int x, y;
        };
        std::vector<Part> parts;
        int left = 1000, top = 1000, right = -1000, bottom = -1000;
        for (unsigned n = 0; n < count; ++n) {
            const auto p = at + 4 + n * 8;
            require(p + 8 <= rom.size() && (rom[p] >> 6) < 3, "Invalid US font object");
            const auto* size = sizes[rom[p] >> 6][rom[p + 1] >> 6];
            Part part{word(rom, p + 2) & 1023, size[0], size[1], std::int16_t(word(rom, p + 4)),
                      std::int16_t(word(rom, p + 6))};
            left = std::min(left, part.x);
            top = std::min(top, part.y);
            right = std::max(right, part.x + int(part.w));
            bottom = std::max(bottom, part.y + int(part.h));
            parts.push_back(part);
        }
        const auto w = right - left, h = bottom - top;
        require(w > 0 && w <= 240 && h > 0 && h <= 160, "Invalid US font image bounds");
        Bytes pixels(w * h);
        for (const auto& part : parts)
            for (unsigned y = 0; y < part.h; ++y)
                for (unsigned x = 0; x < part.w; ++x) {
                    const auto tile = part.tile + (y / 8) * (part.w / 8) + x / 8,
                               off = tile * 32 + (y % 8) * 4 + (x % 8) / 2;
                    require(off < tiles.size(), "US font tile exceeds resource");
                    const auto value = (tiles[off] >> (4 * (x & 1))) & 15;
                    if (value)
                        pixels[(part.y - top + y) * w + part.x - left + x] = std::uint8_t(value);
                }
        put(out, std::uint32_t(left));
        put(out, std::uint32_t(top));
        put(out, w);
        put(out, h);
        out.insert(out.end(), pixels.begin(), pixels.end());
    }
    std::filesystem::create_directories(cache.parent_path());
    auto pending = cache;
    pending += ".pending";
    std::ofstream file(pending, std::ios::binary | std::ios::trunc);
    require(bool(file.write(reinterpret_cast<const char*>(out.data()), out.size())),
            "Cannot write local US font cache");
    file.close();
    require(!file.fail(), "Cannot finish local US font cache");
    require(MoveFileExW(pending.c_str(), cache.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
            "Cannot install local US font cache");
}
void UsTrainingArt::load(const std::filesystem::path& cache) {
    images_.clear();
    const auto bytes = read(cache, 65536);
    require(bytes.size() >= 76 && std::equal(bytes.begin(), bytes.begin() + 8, magic) &&
                std::equal(bytes.begin() + 8, bytes.begin() + 72, identity) &&
                dword(bytes, 72) == 19,
            "Unsupported US Training font cache");
    std::vector<Image> parsed;
    std::size_t at = 76;
    for (unsigned frame = 0; frame < 19; ++frame) {
        Image image;
        image.x = std::int32_t(dword(bytes, at));
        image.y = std::int32_t(dword(bytes, at + 4));
        image.w = dword(bytes, at + 8);
        image.h = dword(bytes, at + 12);
        at += 16;
        require(image.w && image.w <= 240 && image.h && image.h <= 160 &&
                    at + image.w * image.h <= bytes.size(),
                "Invalid US Training font cache image");
        image.pixels.assign(bytes.begin() + at, bytes.begin() + at + image.w * image.h);
        at += image.w * image.h;
        require(
            std::all_of(image.pixels.begin(), image.pixels.end(), [](auto p) { return p <= 15; }),
            "Invalid US Training font pixels");
        parsed.push_back(std::move(image));
    }
    require(at == bytes.size(), "Trailing US Training font cache data");
    images_ = std::move(parsed);
}
} // namespace seed
