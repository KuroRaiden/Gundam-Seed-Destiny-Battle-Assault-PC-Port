#include "title_logo.h"
#include "platform.h"
#include "game_addresses.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace seed::launcher {
namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool value) {
    if (!value)
        throw std::runtime_error("Invalid Japanese title-logo resource.");
}
unsigned u16(const Bytes& bytes, std::size_t at) {
    require(at + 2 <= bytes.size());
    return bytes[at] | (unsigned(bytes[at + 1]) << 8);
}
unsigned u32(const Bytes& bytes, std::size_t at) {
    return u16(bytes, at) | (u16(bytes, at + 2) << 16);
}
Bytes resource(const Bytes& rom, unsigned index) {
    constexpr auto archive = address::jp::kResourceArchive - address::common::kRomBegin;
    constexpr unsigned group = 2;
    const auto table = archive + u32(rom, archive + group * 4);
    const auto end = archive + u32(rom, archive + (group + 1) * 4);
    const auto header = u16(rom, table);
    require(header % 2 == 0 && index + 1 < header / 2);
    auto at = table + u16(rom, table + index * 2);
    const auto next = table + u16(rom, table + (index + 1) * 2);
    require(at >= table + header && at + 2 <= next && next <= end && end <= rom.size());
    if (rom[at] != 'L' || rom[at + 1] != 'e')
        return Bytes(rom.begin() + at, rom.begin() + next);
    require(at + 6 <= next);
    const auto size =
        unsigned(rom[at + 2]) | (unsigned(rom[at + 3]) << 8) | (unsigned(rom[at + 4]) << 16);
    require(size && size <= 16384);
    at += 6;
    Bytes out;
    auto byte = [&] {
        require(at < next);
        return rom[at++];
    };
    while (out.size() < size) {
        auto flags = byte();
        for (unsigned token = 0; token < 4 && out.size() < size; ++token, flags >>= 2) {
            const auto kind = flags & 3;
            if (kind >= 2) {
                for (unsigned n = 0; n < (kind == 2 ? 1u : 3u); ++n)
                    out.push_back(byte());
            } else {
                unsigned distance, length;
                if (kind == 1) {
                    const auto value = byte();
                    distance = (value & 3) + 1;
                    length = (value >> 2) + 2;
                } else {
                    const auto low = byte();
                    const unsigned value = low | (unsigned(byte()) << 8);
                    distance = (value & 4095) + 5;
                    length = (value >> 12) + 3;
                }
                require(distance <= out.size());
                while (length--)
                    out.push_back(out[out.size() - distance]);
            }
            require(out.size() <= size);
        }
    }
    return out;
}
} // namespace
TitleLogo load_title_logo(const std::filesystem::path& path) {
    require(std::filesystem::file_size(path) == 8 * 1024 * 1024 &&
            file_hash(path, false) ==
                "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb");
    std::ifstream input(path, std::ios::binary);
    Bytes rom(8 * 1024 * 1024);
    require(bool(input.read(reinterpret_cast<char*>(rom.data()), rom.size())));
    const auto tiles = resource(rom, 13), map = resource(rom, 14), palette = resource(rom, 15);
    require(tiles.size() == 8000 && map.size() >= 1216 && palette.size() == 256 &&
            u32(map, 0) == 30 && u32(map, 4) == 20);
    Bytes full(240 * 160 * 4);
    unsigned left = 240, top = 160, right = 0, bottom = 0;
    for (unsigned y = 0; y < 160; ++y)
        for (unsigned x = 0; x < 240; ++x) {
            const auto tile = u16(map, 16 + ((y / 8) * 30 + x / 8) * 2);
            const auto px = tile & 1024 ? 7 - x % 8 : x % 8;
            const auto py = tile & 2048 ? 7 - y % 8 : y % 8;
            const auto offset = (tile & 1023) * 32 + py * 4 + px / 2;
            require(offset < tiles.size());
            const auto index = (tiles[offset] >> (4 * (px & 1))) & 15;
            if (!index)
                continue;
            const auto color = u16(palette, (tile >> 12) * 32 + index * 2);
            auto* pixel = full.data() + (y * 240 + x) * 4;
            for (unsigned c = 0; c < 3; ++c)
                pixel[c] = std::uint8_t(((color >> (c * 5)) & 31) * 255 / 31);
            pixel[3] = 255;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    require(right > left && bottom > top);
    TitleLogo logo{right - left, bottom - top};
    for (unsigned y = top; y < bottom; ++y)
        logo.rgba.insert(logo.rgba.end(), full.begin() + (y * 240 + left) * 4,
                         full.begin() + (y * 240 + right) * 4);
    return logo;
}
} // namespace seed::launcher
