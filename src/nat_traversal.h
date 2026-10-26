#pragma once
#include "invite_code.h"
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace seed {
struct DiscoveryServer {
    std::string host;
    std::uint16_t port;
};
struct NatOptions {
    std::vector<DiscoveryServer> stun{{"stun.l.google.com", 19302}};
    DiscoveryServer autopunch{"delthas.fr", 14763};
    unsigned timeout_ms = 6000;
    std::uint32_t local_ipv4_override = 0; // Explicit developer fixture only.
};
struct NatInfo {
    NetworkEndpoint public_endpoint;
    std::uint32_t lan_ipv4 = 0;
    bool searching = true, autopunch_reachable = false;
};
// STUN discovery and AutoPunch-compatible rendezvous share the game socket.
// No injected DLL, background socket or administrator permission is needed.
class NatTraversal {
  public:
    using Send = std::function<bool(NetworkEndpoint, std::span<const std::uint8_t>)>;
    using Mapping = std::function<void(NetworkEndpoint)>;
    NatTraversal(std::uint16_t local_port, Send send, Mapping mapping, NatOptions options = {});
    ~NatTraversal();
    void target(NetworkEndpoint address, std::uint16_t internal_port);
    void tick();
    bool receive(NetworkEndpoint source, std::span<const std::uint8_t> data);
    void confirmed();
    NatInfo info() const;
    static bool same_lan(std::uint32_t peer);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace seed
