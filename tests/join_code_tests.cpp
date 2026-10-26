#include "invite_code.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    try {
        seed::JoinCode original;
        const std::uint8_t public_ip[]{198, 51, 100, 42}, local_ip[]{192, 168, 1, 8};
        std::memcpy(&original.public_endpoint.ipv4, public_ip, 4);
        std::memcpy(&original.lan_ipv4, local_ip, 4);
        original.public_endpoint.port = 41234;
        original.local_port = 51234;
        original.internet = original.autopunch = true;
        for (unsigned i = 0; i < 16; ++i)
            original.token[i] = std::uint8_t(i * 13 + 7);
        const auto code = seed::encode_join_code(original), link = seed::invitation_link(original);
        require(code.size() == 52 && code.starts_with("SEED1-"), "Code is not compact/canonical");
        for (const auto text : {code, link, " \t" + link + "\r\n"}) {
            const auto parsed = seed::decode_join_code(text);
            require(parsed.public_endpoint == original.public_endpoint &&
                        parsed.lan_ipv4 == original.lan_ipv4 &&
                        parsed.local_port == original.local_port &&
                        parsed.token == original.token && parsed.internet && parsed.autopunch,
                    "Join code lost connection identity");
        }
        for (std::size_t i = 6; i < code.size(); ++i) {
            auto damaged = code;
            damaged[i] = damaged[i] == 'A' ? 'B' : 'A';
            bool rejected = false;
            try {
                seed::decode_join_code(damaged);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Damaged code was accepted");
        }
        for (const auto text : std::array<std::string, 6>{
                 std::string(), code.substr(0, code.size() - 1), code + "A", "seedpc://join/",
                 link + "?extra=1", std::string(600, 'A')}) {
            bool rejected = false;
            try {
                seed::decode_join_code(text);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Malformed code/link was accepted");
        }
        std::puts("PASS: compact code/link identity, whitespace handling, per-character corruption "
                  "and malformed input rejection.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
