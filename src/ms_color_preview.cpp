#include "ms_color_preview.h"
#include <fstream>
#include <stdexcept>
namespace seed {
std::map<unsigned, MsColorPreview> load_ms_previews(const std::filesystem::path& path) {
    auto fail = [] {
        throw std::runtime_error("Invalid MS preview cache. Prepare previews again.");
    };
    if (std::filesystem::file_size(path) > 8 * 1024 * 1024)
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
    const char* magic = "SEEDMS";
    for (unsigned n = 0; n < 6; ++n)
        if (byte() != unsigned(magic[n]))
            fail();
    const auto version = byte();
    if ((version != '1' && version != '2') || byte() != 0 || u32() != 27)
        fail();
    const auto sprite_count = version == '2' ? u32() : 1;
    if (sprite_count != (version == '2' ? kMsPreviewSpriteCount : 1))
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
        const auto id = u32();
        if (id < 1 || id > 27 || result.contains(id))
            fail();
        MsColorPreview preview;
        auto read_sprite = [&] {
            MsColorSprite sprite;
            sprite.width = u32();
            sprite.height = u32();
            if (!sprite.width || !sprite.height || sprite.width > 240 || sprite.height > 160)
                fail();
            return sprite;
        };
        if (version == '1')
            preview.sprites.push_back(read_sprite());
        for (auto& palette : preview.originals)
            for (auto& color : palette) {
                const auto low = byte();
                color = std::uint16_t(low | (byte() << 8));
                if (color > 0x7fff)
                    fail();
            }
        for (unsigned pose = 0; pose < sprite_count; ++pose) {
            if (version == '2')
                preview.sprites.push_back(read_sprite());
            auto& sprite = preview.sprites[pose];
            sprite.indices.resize(sprite.width * sprite.height);
            unsigned visible = 0;
            for (auto& index : sprite.indices) {
                index = std::uint8_t(byte());
                if (index != 255 && (index >= kMsPaletteColors || index % 16 == 0))
                    fail();
                visible += index != 255;
            }
            if (visible <= 80)
                fail();
        }
        result.emplace(id, std::move(preview));
    }
    if (file.peek() != std::char_traits<char>::eof())
        fail();
    return result;
}
std::vector<std::uint8_t>
MsColorSprite::rgba(const std::array<std::uint16_t, kMsPaletteColors>& palette) const {
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
