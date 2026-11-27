#pragma once
#include <memory>
#include <string>
#include <vector>
namespace seed {
inline constexpr char kMatchmakingUrl[] =
    "https://seedpc-matchmaking.lord19uga.workers.dev/v1/connect";
inline constexpr char kSpectatingUrl[] =
    "https://seedpc-matchmaking.lord19uga.workers.dev/v1/spectate";
// WinHTTP and all its callbacks stay off the game thread. Cancel only signals
// the job; completed jobs are joined when polled, never during an active match.
class MatchmakingSocket {
  public:
    MatchmakingSocket();
    ~MatchmakingSocket();
    MatchmakingSocket(const MatchmakingSocket&) = delete;
    MatchmakingSocket& operator=(const MatchmakingSocket&) = delete;
    void connect(const std::string& url = kMatchmakingUrl);
    void disconnect();
    void send(std::string text);
    bool flush(unsigned timeout_ms);
    std::vector<std::string> take();
    bool active() const;

  private:
    struct Job;
    std::unique_ptr<Job> job_;
    std::vector<std::unique_ptr<Job>> retired_;
    void collect();
};
} // namespace seed
