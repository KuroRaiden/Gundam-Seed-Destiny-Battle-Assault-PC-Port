#include "quick_match.h"
#include "online_room.h"
#include "mini_json.h"
#include "player_profile.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace seed {
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool valid_region(const std::string& s) {
    return s == "any" || s == "na" || s == "eu" || s == "as" || s == "oc" || s == "sa" || s == "af";
}
bool valid_id(const std::string& s) {
    return s.size() == 36 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
           });
}
std::string field(const char* key, const std::string& value) {
    std::string out = ",\"" + std::string(key) + "\":";
    gbarecomp::json::append_quoted(out, value);
    return out;
}
int count(const gbarecomp::json::Value& event, const char* key) {
    const auto* value = event.get(key);
    require(value && value->is_number() && std::isfinite(value->number) && value->number >= 0 &&
                value->number <= 1024 && std::floor(value->number) == value->number,
            "Invalid queue count.");
    return int(value->number);
}
} // namespace
void QuickMatchProtocol::start(std::string name, std::string region, bool search) {
    require(valid_region(region), "Invalid matchmaking region.");
    *this = {};
    name_ = normalize_player_name(name);
    region_ = std::move(region);
    view_.phase = QuickMatchPhase::Connecting;
    view_.searching = search;
    view_.status = "Connecting to Quick Match...";
}
void QuickMatchProtocol::send(const char* type, const std::string& code) {
    std::string message = "{\"protocol\":1,\"type\":\"" + std::string(type) + "\"";
    if (std::string(type) == "hello") {
        message += field("build", kMatchmakingCompatibility) + field("name", name_) +
                   field("region", region_);
    } else if (!match_.empty())
        message += field("match", match_);
    if (!code.empty())
        message += field("code", code);
    messages_.push_back(message + '}');
}
void QuickMatchProtocol::fail(const std::string& reason) {
    release_ = true;
    // Service loss after a native connection cannot interrupt selection, combat
    // or rematches. From this point the existing room owns the entire session.
    if (view_.phase == QuickMatchPhase::Connected)
        return;
    view_.phase = QuickMatchPhase::Error;
    view_.searching = false;
    view_.status = reason;
    view_.waiting = view_.eligible = -1;
    actions_.push_back({QuickRoomAction::Close, {}});
}
void QuickMatchProtocol::receive(const std::string& text) {
    try {
        require(text.size() <= 4096, "Service message is too large.");
        gbarecomp::json::Value event;
        require(gbarecomp::json::parse(text, event) && event.is_object(),
                "Invalid matchmaking response.");
        const auto type = event.str("type");
        if (type == "socket_open") {
            send("hello");
            return;
        }
        if (type == "socket_error") {
            fail(event.str("message", "Matchmaking disconnected."));
            return;
        }
        const auto* protocol = event.get("protocol");
        require(protocol && protocol->is_number() && protocol->number == 1,
                "Incompatible matchmaking service. Update the game.");
        if (type == "welcome" || type == "published")
            return;
        if (type == "count" || type == "ready") {
            require(event.str("region") == region_, "Unexpected matchmaking region.");
            view_.waiting = count(event, "waiting");
            view_.eligible = count(event, "eligible");
            if (type == "ready") {
                require(view_.phase == QuickMatchPhase::Connecting,
                        "Unexpected matchmaking acknowledgement.");
                view_.phase = view_.searching ? QuickMatchPhase::Searching : QuickMatchPhase::Ready;
                view_.status =
                    view_.searching ? "Looking for an opponent..." : "Ready to find a match.";
                if (view_.searching)
                    send("enqueue");
            }
        } else if (type == "searching") {
            if (view_.phase == QuickMatchPhase::Connected)
                return;
            match_.clear();
            role_.clear();
            published_.clear();
            host_issued_ = joined_ = false;
            actions_.push_back({QuickRoomAction::Close, {}});
            view_.phase = QuickMatchPhase::Searching;
            view_.searching = true;
            view_.opponent.clear();
            view_.status = event.str("reason").empty()
                               ? "Looking for an opponent..."
                               : "Opponent left. Looking for another player...";
        } else if (type == "match_found") {
            const auto id = event.str("match"), role = event.str("role");
            require(view_.phase == QuickMatchPhase::Searching && valid_id(id) &&
                        (role == "host" || role == "guest"),
                    "Invalid matchmaking offer.");
            match_ = id;
            role_ = role;
            view_.opponent = normalize_player_name(event.str("opponent"));
            view_.phase = QuickMatchPhase::ConnectingRoom;
            view_.status = "Match found. Connecting to " + view_.opponent + "...";
            send("accept");
        } else if (type == "prepare" || type == "preparing" || type == "connect") {
            require(view_.phase == QuickMatchPhase::ConnectingRoom && event.str("match") == match_,
                    "Stale matchmaking connection.");
            if (type == "prepare") {
                require(role_ == "host", "Unexpected host assignment.");
                if (!host_issued_) {
                    actions_.push_back({QuickRoomAction::Host, {}});
                    host_issued_ = true;
                }
            } else if (type == "preparing")
                require(role_ == "guest", "Unexpected guest assignment.");
            else {
                require(role_ == "guest", "Unexpected invitation recipient.");
                const auto code = event.str("code");
                require(code.starts_with("SEED1-") && code.size() == 52,
                        "Invalid matchmaking invitation.");
                decode_join_code(code);
                if (!joined_) {
                    actions_.push_back({QuickRoomAction::Join, code});
                    joined_ = true;
                }
            }
        } else if (type == "handoff") {
            require(view_.phase == QuickMatchPhase::Connected && event.str("match") == match_,
                    "Unexpected session handoff.");
            release_ = true;
        } else if (type == "idle") {
            if (view_.phase == QuickMatchPhase::Connected)
                return;
            view_.phase = QuickMatchPhase::Ready;
            view_.searching = false;
            view_.status = "Connection timed out. Find another match.";
            actions_.push_back({QuickRoomAction::Close, {}});
        } else if (type == "error") {
            const auto reason = event.str("reason");
            fail(reason == "incompatible_build"
                     ? "Different game version. Install the latest compatible build."
                     : "Quick Match could not continue. Try again or use Host / Join.");
        } else
            require(false, "Unrecognized matchmaking response.");
    } catch (const std::exception& error) {
        fail(error.what());
    }
}
void QuickMatchProtocol::publish(const std::string& code) {
    if (view_.phase != QuickMatchPhase::ConnectingRoom || role_ != "host" || !host_issued_ ||
        code.empty() || code == published_)
        return;
    try {
        decode_join_code(code);
        published_ = code;
        send("publish", code);
    } catch (const std::exception& error) {
        fail(error.what());
    }
}
void QuickMatchProtocol::connected() {
    if (view_.phase != QuickMatchPhase::ConnectingRoom)
        return;
    view_.phase = QuickMatchPhase::Connected;
    view_.searching = false;
    view_.status = "Connected. Choose your mobile suits.";
    send("connected");
}
std::vector<std::string> QuickMatchProtocol::take_messages() {
    return std::exchange(messages_, {});
}
std::vector<QuickRoomAction> QuickMatchProtocol::take_actions() {
    return std::exchange(actions_, {});
}
void QuickMatchClient::open(const std::string& name, const std::string& region, bool search) {
    name_ = normalize_player_name(name);
    region_ = region;
    try {
        protocol_.start(name_, region_, search);
        socket_.connect(url_);
    } catch (const std::exception& error) {
        protocol_.fail(error.what());
    }
}
void QuickMatchClient::observe(const std::string& name, const std::string& region) {
    if (protocol_.view().busy() || protocol_.view().phase == QuickMatchPhase::Connected)
        return;
    if (protocol_.view().phase == QuickMatchPhase::Offline || region_ != region)
        open(name, region, false);
}
void QuickMatchClient::search(const std::string& name, const std::string& region) {
    open(name, region, true);
}
void QuickMatchClient::cancel() {
    socket_.disconnect();
    protocol_ = {};
    name_.clear();
    region_.clear();
}
void QuickMatchClient::failed(const std::string& reason) {
    cancel();
    protocol_.fail(reason);
}
void QuickMatchClient::fixture(std::string url, std::string host) {
    cancel();
    url_ = std::move(url);
    fixture_host_ = std::move(host);
}
void QuickMatchClient::tick(OnlineRoom& room, LocalVsSelection& selection) {
    // A peer handshake can complete on the preceding native frame while a
    // service disconnect/requeue is in flight. Established P2P ownership wins
    // before consuming that event, so its key can never enter a second offer.
    if (room.connected())
        protocol_.connected();
    for (const auto& message : socket_.take())
        protocol_.receive(message);
    for (const auto& action : protocol_.take_actions()) {
        // Never let a service error or late cancellation close an established room.
        if (room.connected())
            continue;
        try {
            room.close();
            selection.cancelled = false;
            for (auto& seat : selection.seats)
                seat.ready = false;
            if (action.kind == QuickRoomAction::Host)
                room.host(0, fixture_host_, {}, name_);
            else if (action.kind == QuickRoomAction::Join)
                room.join(action.code, {}, name_);
        } catch (const std::exception& error) {
            protocol_.fail(error.what());
        }
    }
    if (room.connected())
        protocol_.connected();
    else if (room.failed() && protocol_.view().busy())
        protocol_.fail(room.status());
    else
        protocol_.publish(room.code());
    try {
        for (auto& message : protocol_.take_messages())
            socket_.send(std::move(message));
    } catch (const std::exception& error) {
        protocol_.fail(error.what());
    }
    if (protocol_.release_socket())
        socket_.disconnect();
}
} // namespace seed
