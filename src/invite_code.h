#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace seed {
struct NetworkEndpoint {
    std::uint32_t ipv4 = 0; // IPv4 bytes in network order; port in host order.
    std::uint16_t port = 0;
    bool operator==(const NetworkEndpoint&) const = default;
    explicit operator bool() const {
        return ipv4 && port;
    }
};
struct JoinCode {
    NetworkEndpoint public_endpoint;
    std::uint32_t lan_ipv4 = 0;
    std::uint16_t local_port = 0;
    std::array<std::uint8_t, 16> token{};
    bool internet = false, autopunch = false;
};
std::string encode_join_code(const JoinCode& value);
JoinCode decode_join_code(const std::string& text); // Code or seedpc://join/code.
std::string invitation_link(const JoinCode& value);
bool valid_peer_ipv4(std::uint32_t address);
} // namespace seed
