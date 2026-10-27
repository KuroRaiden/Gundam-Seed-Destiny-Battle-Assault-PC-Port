#include "ms_color_preview.h"
#include <fstream>
#include <stdexcept>
namespace seed {
std::map<unsigned, MsColorPreview> load_ms_previews(const std::filesystem::path& path) {
    auto fail = [] {
        throw std::runtime_error("Invalid MS preview cache. Prepare previews again.");
    };
    if (std::filesystem::file_size(path) > 2 * 1024 * 1024)
        fail();
    std::ifstream file(path, std::ios::binary);
    auto byte = [&] {
        const auto value = file.get();
        if (value < 0)
            fail();
        return unsigned(value);
    };
    auto u32 = [&] {
        unsigned value = 0;
        for (unsigned n = 0; n < 4; ++n)
            value |= byte() << (n * 8);
        return value;
    };
    const std::array<unsigned char, 8> magic{'S', 'E', 'E', 'D', 'M', 'S', '1', 0};
    for (auto c : magic)
        if (byte() != c)
            fail();
    if (u32() != 27)
        fail();
    const char* expected = "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb";
    const char* digits = "0123456789abcdef";
    for (unsigned n = 0; n < 32; ++n) {
        auto b = byte();
        if (digits[b >> 4] != expected[n * 2] || digits[b & 15] != expected[n * 2 + 1])
            fail();
    }
    std::map<unsigned, MsColorPreview> result;
    for (unsigned n = 0; n < 27; ++n) {
        const auto id = u32(), w = u32(), h = u32();
        if (id < 1 || id > 27 || result.contains(id) || !w || !h || w > 240 || h > 160)
            fail();
        MsColorPreview preview;
        preview.width = w;
        preview.height = h;
        for (auto& palette : preview.originals)
            for (auto& color : palette) {
                const auto low = byte();
                color = std::uint16_t(low | (byte() << 8));
                if (color > 0x7fff)
                    fail();
            }
        preview.indices.resize(w * h);
        unsigned visible = 0;
        for (auto& index : preview.indices) {
            index = std::uint8_t(byte());
            if (index != 255 && (index >= kMsPaletteColors || index % 16 == 0))
                fail();
            visible += index != 255;
        }
        if (visible <= 80)
            fail();
        result.emplace(id, std::move(preview));
    }
    if (file.peek() != std::char_traits<char>::eof())
        fail();
    return result;
}
std::vector<std::uint8_t>
MsColorPreview::rgba(const std::array<std::uint16_t, kMsPaletteColors>& palette) const {
    std::vector<std::uint8_t> pixels(indices.size() * 4);
    for (unsigned n = 0; n < indices.size(); ++n) {
        const auto index = indices[n];
        if (index == 255)
            continue;
        const auto c = palette[index];
        for (unsigned channel = 0; channel < 3; ++channel)
            pixels[n * 4 + channel] = std::uint8_t(((c >> (channel * 5)) & 31) * 255 / 31);
        pixels[n * 4 + 3] = 255;
    }
    return pixels;
}
} // namespace seed
