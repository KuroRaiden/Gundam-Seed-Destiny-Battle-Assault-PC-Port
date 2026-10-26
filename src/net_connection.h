#pragma once
#include <array>
#include <cstdint>
namespace seed {
// POD boundary between the US menu executable and the JP simulation DLL.
// The menu owns the socket for the whole room. Combat borrows it on the same
// thread, so no port rebind or NAT mapping change occurs during handoff.
struct NetConnection {
    std::uint64_t socket = ~std::uint64_t(0);
    std::uint32_t peer_ipv4 = 0, epoch = 0;
    std::uint16_t local_port = 0, peer_port = 0;
    std::array<std::uint8_t, 16> token{};
    unsigned seat = 0;
};
} // namespace seed
