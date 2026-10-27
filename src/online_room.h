#pragma once
#include "local_vs.h"
#include "udp_transport.h"
#include "player_profile.h"
#include <memory>
#include <string>
namespace seed {
inline constexpr std::uint32_t kRoomProtocol = 0x00080001;
inline constexpr std::uint32_t kRoomMagic = 0x31524353; // SCR1
inline constexpr std::uint32_t kCombatControlMagic = 0x314E504A;
inline constexpr unsigned kCombatControlPacketBytes = 32 + 2 * kMsPaletteBytes;
constexpr std::uint32_t next_match_epoch(std::uint32_t epoch) {
    return epoch == UINT32_MAX ? 1 : epoch + 1;
}
// Startup acknowledgements remain serviceable after the client enters combat.
// This prevents a lost final lobby packet from stranding the host.
bool acknowledge_room_start(UdpTransport& transport, const std::vector<std::uint8_t>& packet,
                            const NetConnection& connection, const std::uint8_t* fighters,
                            const std::uint8_t* colors, const CustomPalette* palettes);
// A peer can reach selection before its opponent receives the last match
// checksum ack. Keep servicing that completed match without leaving the room.
bool acknowledge_match_finish(UdpTransport& transport, const std::vector<std::uint8_t>& packet,
                              const NetConnection& connection, std::uint32_t final_crc);
class OnlineRoom {
  public:
    void host(std::uint16_t port = 0, const std::string& advertised_host = "",
              NatOptions options = {}, const std::string& name = "Player 1");
    void join(const std::string& invitation, NatOptions options = {},
              const std::string& name = "Player 2");
    void close();
    void tick(LocalVsSelection& selection, std::uint16_t local_input);
    bool connected() const {
        return connected_;
    }
    bool launching() const {
        return launch_;
    }
    unsigned seat() const {
        return seat_;
    }
    const std::string& invitation() const {
        return invitation_;
    }
    const std::string& code() const {
        return code_;
    }
    const std::array<std::string, 2>& names() const {
        return names_;
    }
    const std::string& status() const {
        return status_;
    }
    bool active() const {
        return bool(transport_);
    }
    bool failed() const {
        return failed_;
    }
    NetConnection connection();
    void match_returned(LocalVsSelection& selection, std::uint32_t final_crc);
    std::uint32_t epoch() const {
        return epoch_;
    }
    std::uint64_t completed_matches() const {
        return completed_matches_;
    }

  private:
    void reset(bool notify_peer);
    void send(unsigned kind, const LocalVsSelection* selection = nullptr);
    std::unique_ptr<UdpTransport> transport_;
    MatchInputs inputs_;
    std::string code_, invitation_, status_ = "Create a room or paste an invitation.";
    unsigned seat_ = 0;
    std::uint32_t sequence_ = 0, remote_sequence_ = 0, epoch_ = 0;
    std::uint64_t last_receive_ = 0, last_send_ = 0, created_ = 0;
    bool connected_ = false, launch_ = false, pending_ = false, remote_seen_ = false,
         failed_ = false, awaiting_peer_ = false, release_guard_ = false, completed_ = false;
    std::uint32_t completed_epoch_ = 0, completed_crc_ = 0;
    std::uint64_t completed_matches_ = 0;
    std::array<std::uint8_t, 2> fighters_{}, colors_{};
    std::array<CustomPalette, 2> palettes_{};
    std::array<std::string, 2> names_{"Player 1", "Player 2"};
};
} // namespace seed
