#pragma once
#include "matchmaking_socket.h"
#include <string>
#include <vector>
namespace seed {
class OnlineRoom;
struct LocalVsSelection;
inline constexpr char kMatchmakingCompatibility[] = "seedpc-jp-r10-a8-20261028";
enum class QuickMatchPhase {
    Offline,
    Connecting,
    Ready,
    Searching,
    ConnectingRoom,
    Connected,
    Error
};
struct QuickMatchView {
    QuickMatchPhase phase = QuickMatchPhase::Offline;
    int waiting = -1, eligible = -1;
    bool searching = false;
    std::string status = "Find another player without exchanging codes.", opponent;
    bool busy() const {
        return searching && phase != QuickMatchPhase::Connected && phase != QuickMatchPhase::Error;
    }
};
struct QuickRoomAction {
    enum Kind { Close, Host, Join } kind;
    std::string code;
};
// Deterministic service protocol model; it never reads or changes guest state.
class QuickMatchProtocol {
  public:
    void start(std::string name, std::string region, bool search);
    void receive(const std::string& text);
    void publish(const std::string& code);
    void connected();
    void fail(const std::string& reason);
    const QuickMatchView& view() const {
        return view_;
    }
    std::vector<std::string> take_messages();
    std::vector<QuickRoomAction> take_actions();
    bool release_socket() const {
        return release_;
    }

  private:
    void send(const char* type, const std::string& code = {});
    QuickMatchView view_;
    std::string name_, region_, match_, role_, published_;
    bool release_ = false, host_issued_ = false, joined_ = false;
    std::vector<std::string> messages_;
    std::vector<QuickRoomAction> actions_;
};
class QuickMatchClient {
  public:
    void observe(const std::string& name, const std::string& region);
    void search(const std::string& name, const std::string& region);
    void cancel();
    void failed(const std::string& reason);
    void tick(OnlineRoom& room, LocalVsSelection& selection);
    const QuickMatchView& view() const {
        return protocol_.view();
    }
    // Explicit developer fixtures only; these options are never exposed to players.
    void fixture(std::string url, std::string advertised_host);

  private:
    void open(const std::string& name, const std::string& region, bool search);
    MatchmakingSocket socket_;
    QuickMatchProtocol protocol_;
    std::string name_, region_, url_ = kMatchmakingUrl, fixture_host_;
};
} // namespace seed
