#include "invite_code.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace seed {
namespace {
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
void put16(std::uint8_t* p, unsigned n) {
    p[0] = std::uint8_t(n >> 8);
    p[1] = std::uint8_t(n);
}
unsigned get16(const std::uint8_t* p) {
    return (unsigned(p[0]) << 8) | p[1];
}
void invalid() {
    throw std::runtime_error(
        "That join code is incomplete or contains a typo. Copy it again from the host.");
}
} // namespace
bool valid_peer_ipv4(std::uint32_t address) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&address);
    return bytes[0] != 0 && bytes[0] < 224 && address != 0xFFFFFFFFu;
}
std::string encode_join_code(const JoinCode& value) {
    if (!value.public_endpoint || !valid_peer_ipv4(value.public_endpoint.ipv4) ||
        !value.local_port || (value.lan_ipv4 && !valid_peer_ipv4(value.lan_ipv4)))
        invalid();
    std::array<std::uint8_t, 34> bytes{};
    bytes[0] = 1;
    bytes[1] = (value.internet ? 1 : 0) | (value.autopunch ? 2 : 0);
    std::memcpy(bytes.data() + 2, &value.public_endpoint.ipv4, 4);
    put16(bytes.data() + 6, value.public_endpoint.port);
    std::memcpy(bytes.data() + 8, &value.lan_ipv4, 4);
    put16(bytes.data() + 12, value.local_port);
    std::copy(value.token.begin(), value.token.end(), bytes.begin() + 14);
    const auto crc = SDL_crc32(0, bytes.data(), 30);
    for (unsigned i = 0; i < 4; ++i)
        bytes[30 + i] = std::uint8_t(crc >> (8 * i));
    std::string result = "SEED1-";
    unsigned buffer = 0, bits = 0;
    for (auto byte : bytes) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            result += alphabet[(buffer >> bits) & 63];
        }
    }
    if (bits)
        result += alphabet[(buffer << (6 - bits)) & 63];
    return result;
}
JoinCode decode_join_code(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n"), last = input.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || input.size() > 512)
        invalid();
    std::string text = input.substr(first, last - first + 1);
    const std::string scheme = "seedpc://join/";
    if (text.starts_with(scheme))
        text.erase(0, scheme.size());
    if (!text.starts_with("SEED1-") || text.size() != 52)
        invalid();
    text.erase(0, 6);
    std::vector<std::uint8_t> bytes;
    unsigned buffer = 0, bits = 0;
    for (const char c : text) {
        const auto* found = std::find(std::begin(alphabet), std::end(alphabet) - 1, c);
        if (found == std::end(alphabet) - 1)
            invalid();
        buffer = (buffer << 6) | unsigned(found - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(std::uint8_t(buffer >> bits));
        }
    }
    if (bytes.size() != 34 || (bits && (buffer & ((1u << bits) - 1))) || bytes[0] != 1 ||
        (bytes[1] & ~3u))
        invalid();
    std::uint32_t expected = 0;
    for (unsigned i = 0; i < 4; ++i)
        expected |= std::uint32_t(bytes[30 + i]) << (8 * i);
    if (expected != SDL_crc32(0, bytes.data(), 30))
        invalid();
    JoinCode value;
    std::memcpy(&value.public_endpoint.ipv4, bytes.data() + 2, 4);
    value.public_endpoint.port = std::uint16_t(get16(bytes.data() + 6));
    std::memcpy(&value.lan_ipv4, bytes.data() + 8, 4);
    value.local_port = std::uint16_t(get16(bytes.data() + 12));
    std::copy_n(bytes.begin() + 14, 16, value.token.begin());
    value.internet = bytes[1] & 1;
    value.autopunch = bytes[1] & 2;
    if (encode_join_code(value) != "SEED1-" + text)
        invalid();
    return value;
}
std::string invitation_link(const JoinCode& value) {
    return "seedpc://join/" + encode_join_code(value);
}
} // namespace seed
