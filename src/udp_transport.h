#pragma once
#include "gekkonet.h"
#include "net_connection.h"
#include "nat_traversal.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace seed {
// One transport per network driver thread. The UDP envelope separates game
// data and lobby/control traffic and rejects stale sessions and damaged data.
class UdpTransport {
  public:
    using Token = std::array<std::uint8_t, 16>;
    struct Counters {
        std::uint64_t sent = 0, received = 0, rejected = 0;
    };
    UdpTransport(std::uint16_t bind_port, const std::string& peer_host, std::uint16_t peer_port,
                 Token token);
    explicit UdpTransport(const NetConnection& borrowed);
    ~UdpTransport();
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;
    void activate();
    GekkoNetAdapter* adapter();
    GekkoNetAddress address();
    void poll();
    bool send_control(std::span<const std::uint8_t> bytes);
    std::vector<std::vector<std::uint8_t>> take_control();
    std::uint16_t local_port() const;
    std::uint16_t peer_port() const;
    std::string peer_host() const;
    Counters counters() const;
    NetConnection borrow(unsigned seat, std::uint32_t epoch) const;
    void set_game_epoch(std::uint32_t epoch);
    void enable_nat(NatOptions options = {});
    void add_candidate(NetworkEndpoint endpoint);
    void punch_target(NetworkEndpoint endpoint, std::uint16_t internal_port);
    NatInfo discovery() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace seed
