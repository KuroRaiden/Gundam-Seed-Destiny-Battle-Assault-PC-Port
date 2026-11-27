#pragma once
#include "match_replay.h"
#include "matchmaking_socket.h"
#include <chrono>
#include <optional>

namespace seed {
inline constexpr unsigned kLiveChunkFrames = 120, kSpectatorDelayFrames = 180;
struct LiveMatchEntry {
    std::string id;
    std::array<std::string, 2> names;
    std::array<unsigned, 2> fighters{};
    unsigned stage = 0, frames = 0, viewers = 0;
};
class LiveMatchesClient {
  public:
    void browse(unsigned page = 0);
    void watch(const std::string& id);
    void tick();
    void close();
    void fixture(std::string url) {
        url_ = std::move(url);
    }
    const std::vector<LiveMatchEntry>& matches() const {
        return matches_;
    }
    const std::string& status() const {
        return status_;
    }
    const std::optional<MatchReplay>& replay() const {
        return replay_;
    }
    bool failed() const {
        return failed_;
    }
    bool ended() const {
        return ended_;
    }
    bool more() const {
        return more_;
    }
    unsigned join_frame() const {
        return join_frame_;
    }

  private:
    void connect();
    MatchmakingSocket socket_;
    std::string url_ = kSpectatingUrl, id_, status_;
    std::vector<LiveMatchEntry> matches_;
    std::optional<MatchReplay> replay_;
    std::chrono::steady_clock::time_point last_request_{};
    bool failed_ = false, ended_ = false, pending_ = false, more_ = false;
    unsigned page_ = 0, join_frame_ = 0;
};
class LiveMatchPublisher {
  public:
    explicit LiveMatchPublisher(const MatchReplay& replay);
    void tick();
    void append(std::span<const std::array<std::uint16_t, 2>> inputs, bool finish = false,
                bool complete = false, std::uint32_t crc = 0);
    bool failed() const {
        return failed_;
    }

  private:
    MatchmakingSocket socket_;
    std::string id_;
    unsigned sent_ = 0;
    bool failed_ = false;
};
} // namespace seed
