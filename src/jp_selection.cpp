#include "jp_selection.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace seed {
namespace {
constexpr std::uint16_t color(unsigned r, unsigned g, unsigned b) {
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
}
constexpr std::uint16_t cyan = color(40, 215, 255), orange = color(255, 155, 25),
                        green = color(80, 255, 130), dark = color(8, 16, 32);
constexpr std::array<std::uint8_t, 32> donor_hash{
    0xcc, 0x31, 0x08, 0x14, 0xce, 0x3a, 0x13, 0x4a, 0x3d, 0x29, 0x75, 0xbb, 0xe0, 0x18, 0x66, 0x03,
    0xa2, 0x98, 0xa5, 0xee, 0x15, 0xe1, 0x79, 0x5d, 0xed, 0xb2, 0x5f, 0x07, 0x78, 0x78, 0x51, 0xeb};
} // namespace
void JpSelectionArt::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("JP selection art is missing: " + path.string() +
                                 ". Launch again to generate it locally from your JP ROM.");
    auto read = [&](void* p, std::size_t n) {
        if (!file.read(static_cast<char*>(p), n))
            throw std::runtime_error("Truncated JP selection art pack");
    };
    auto u32 = [&]() {
        std::array<unsigned char, 4> b{};
        read(b.data(), 4);
        return unsigned(b[0]) | (unsigned(b[1]) << 8) | (unsigned(b[2]) << 16) |
               (unsigned(b[3]) << 24);
    };
    std::array<char, 8> magic{};
    read(magic.data(), 8);
    if (magic != std::array<char, 8>{'S', 'E', 'E', 'D', 'J', 'P', '1', 0} || u32() != 240 ||
        u32() != 160 || u32() != 91)
        throw std::runtime_error("Unsupported JP selection art pack");
    std::array<std::uint8_t, 32> hash{}, grid{};
    read(hash.data(), 32);
    read(grid.data(), 32);
    if (hash != donor_hash || grid != kJpPortraits)
        throw std::runtime_error("JP selection art does not match the verified Destiny donor");
    images_.clear();
    for (unsigned i = 0; i < 91; ++i) {
        const auto id = u32(), w = u32(), h = u32();
        if (!w || !h || w > 240 || h > 160 || images_.contains(id))
            throw std::runtime_error("Invalid JP selection image");
        Image image{w, h, std::vector<std::uint16_t>(w * h)};
        for (auto& pixel : image.pixels) {
            std::array<unsigned char, 2> b{};
            read(b.data(), 2);
            pixel = b[0] | (b[1] << 8);
        }
        images_.emplace(id, std::move(image));
    }
    if (file.peek() != std::char_traits<char>::eof() || images_.at(0).w != 240 ||
        images_.at(0).h != 160)
        throw std::runtime_error("Invalid JP selection background");
    for (unsigned id = 0; id < 30; ++id)
        for (unsigned base : {100u, 200u, 300u})
            if (!images_.contains(base + id))
                throw std::runtime_error("Incomplete JP selection roster art");
    pixels_.resize(240 * 160);
}
void JpSelectionArt::rect(int x, int y, int w, int h, std::uint16_t c) {
    for (int py = std::max(0, y); py < std::min(160, y + h); ++py)
        for (int px = std::max(0, x); px < std::min(240, x + w); ++px)
            pixels_[py * 240 + px] = c;
}
void JpSelectionArt::blit(unsigned id, int x, int y, bool flip) {
    const auto& image = images_.at(id);
    for (unsigned py = 0; py < image.h; ++py)
        for (unsigned px = 0; px < image.w; ++px) {
            const auto p = image.pixels[py * image.w + (flip ? image.w - 1 - px : px)];
            if (!(p & 0x8000) && x + int(px) >= 0 && x + int(px) < 240 && y + int(py) >= 0 &&
                y + int(py) < 160)
                pixels_[(y + py) * 240 + x + px] = p;
        }
}
void JpSelectionArt::text(int x, int y, const std::string& value, std::uint16_t c) {
    static constexpr std::uint8_t letters[][5] = {
        {126, 9, 9, 9, 126},   {127, 73, 73, 73, 54}, {62, 65, 65, 65, 34},  {127, 65, 65, 34, 28},
        {127, 73, 73, 73, 65}, {127, 9, 9, 9, 1},     {62, 65, 73, 73, 122}, {127, 8, 8, 8, 127},
        {65, 65, 127, 65, 65}, {32, 64, 65, 63, 1},   {127, 8, 20, 34, 65},  {127, 64, 64, 64, 64},
        {127, 2, 12, 2, 127},  {127, 4, 8, 16, 127},  {62, 65, 65, 65, 62},  {127, 9, 9, 9, 6},
        {62, 65, 81, 33, 94},  {127, 9, 25, 41, 70},  {70, 73, 73, 73, 49},  {1, 1, 127, 1, 1},
        {63, 64, 64, 64, 63},  {31, 32, 64, 32, 31},  {63, 64, 56, 64, 63},  {99, 20, 8, 20, 99},
        {7, 8, 112, 8, 7},     {97, 81, 73, 69, 67}};
    for (char ch : value) {
        std::array<std::uint8_t, 5> glyph{};
        if (ch >= 'A' && ch <= 'Z')
            std::copy_n(letters[ch - 'A'], 5, glyph.begin());
        else if (ch == '1')
            glyph = {0, 66, 127, 64, 0};
        else if (ch == '2')
            glyph = {98, 81, 73, 73, 70};
        else if (ch == '3')
            glyph = {34, 65, 73, 73, 54};
        else if (ch == '4')
            glyph = {24, 20, 18, 127, 16};
        else if (ch == '5')
            glyph = {39, 69, 69, 69, 57};
        else if (ch == '6')
            glyph = {60, 74, 73, 73, 48};
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if (glyph[col] & (1u << row))
                    rect(x + col, y + row, 1, 1, c);
        x += 6;
    }
}
const std::vector<std::uint16_t>& JpSelectionArt::compose(const LocalVsSelection& s) {
    pixels_ = images_.at(0).pixels;
    for (unsigned player = 0; player < 2; ++player) {
        const auto& seat = s.seats[player];
        const auto id = jp_color_portrait(seat.fighter, seat.color);
        if (id < 30) {
            blit(100 + id, player ? 63 : 80, player ? 80 : 0, player != 0);
            blit(200 + id, player ? 127 : 56, player ? 88 : 8, player != 0);
            blit(300 + id, 80, player ? 136 : 56);
        }
    }
    for (unsigned player = 0; player < 2; ++player) {
        const auto& seat = s.seats[player];
        const auto column = seat.cursor % 8, row = seat.cursor / 8;
        const int x = column == 3   ? 59
                      : column == 4 ? 166
                      : column < 3  ? 4 + 18 * column
                                    : 184 + 18 * (column - 5);
        const int y = (column == 3 || column == 4) ? 65 : 16 + 32 * row;
        const int inset = (player == 1 && s.seats[0].cursor == seat.cursor) ? 1 : 0;
        const auto c = seat.ready ? green : player ? orange : cyan;
        rect(x - inset, y - inset, 16 + inset * 2, 1, c);
        rect(x - inset, y + 23 + inset, 16 + inset * 2, 1, c);
        rect(x - inset, y - inset, 1, 24 + inset * 2, c);
        rect(x + 15 + inset, y - inset, 1, 24 + inset * 2, c);
        rect(x, y + 24, s.training && player ? 20 : 16, 8, dark);
        text(x + 2, y + 24, player ? (s.training ? "COM" : "2P") : "1P", c);
    }
    rect(0, 144, 240, 16, dark);
    std::string line;
    for (unsigned player = 0; player < 2; ++player) {
        const auto& seat = s.seats[player];
        if (player)
            line += "  ";
        line += player ? "2P C" : "1P C";
        line += char('1' + seat.color);
        line += " ";
        line += seat.ready                                            ? "READY"
                : (seat.fighter != 255 && !s.supported(seat.fighter)) ? "NOT PORTED"
                                                                      : "SELECT";
    }
    text((240 - int(line.size() * 6)) / 2, 144, line, cyan);
    if (s.training) {
        rect(0, 144, 240, 8, dark);
        const std::string prompt = std::string("1P C") + char('1' + s.seats[0].color) + " COM C" +
                                   char('1' + s.seats[1].color) +
                                   (s.seats[1].ready   ? " READY"
                                    : s.seats[0].ready ? " SELECT COM"
                                                       : " SELECT 1P");
        text((240 - int(prompt.size() * 6)) / 2, 144, prompt, cyan);
    }
    text(36, 153, "A READY  B BACK  START COLOR", color(230, 240, 255));
    return pixels_;
}
} // namespace seed
