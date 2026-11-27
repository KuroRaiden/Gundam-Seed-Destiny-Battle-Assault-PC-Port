#include "live_matches.h"
#include "mini_json.h"
#include "move_list.h"
#include "jp_stages.h"
#include "player_profile.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace seed {
namespace {
using Json = gbarecomp::json::Value;
void require(bool value, const char* error) {
    if (!value)
        throw std::runtime_error(error);
}
std::string quote(const std::string& value) {
    std::string text;
    gbarecomp::json::append_quoted(text, value);
    return text;
}
unsigned number(const Json& value, const char* key, unsigned maximum) {
    const auto* field = value.get(key);
    require(field && field->is_number() && std::isfinite(field->number) && field->number >= 0 &&
                field->number <= maximum && std::floor(field->number) == field->number,
            "Invalid live match response.");
    return unsigned(field->number);
}
std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
std::vector<std::uint8_t> unhex(const std::string& text) {
    require(text.size() <= 3072 && text.size() % 2 == 0, "Invalid live input data.");
    auto digit = [](char c) {
        const auto at = std::string_view("0123456789abcdef").find(c);
        require(at != std::string_view::npos, "Invalid live input data.");
        return unsigned(at);
    };
    std::vector<std::uint8_t> bytes;
    for (unsigned i = 0; i < text.size(); i += 2)
        bytes.push_back(std::uint8_t(digit(text[i]) * 16 + digit(text[i + 1])));
    return bytes;
}
Json parse(const std::string& text) {
    Json value;
    require(text.size() <= 4096 && gbarecomp::json::parse(text, value) && value.is_object(),
            "Invalid spectator service response.");
    if (value.str("type") == "socket_error")
        throw std::runtime_error(value.str("message"));
    if (value.str("type") == "socket_open")
        return value;
    require(number(value, "protocol", 1) == 1, "Update the game to use spectating.");
    if (value.str("type") == "error")
        throw std::runtime_error("Spectating unavailable: " + value.str("reason"));
    return value;
}
std::string request(const char* type, const std::string& id) {
    return "{\"protocol\":1,\"type\":" + quote(type) + ",\"id\":" + quote(id);
}
} // namespace
void LiveMatchesClient::connect() {
    failed_ = ended_ = pending_ = false;
    matches_.clear();
    replay_.reset();
    status_ = "Connecting to live matches...";
    socket_.connect(url_);
}
void LiveMatchesClient::browse(unsigned page) {
    try {
        id_.clear();
        page_ = page;
        connect();
        socket_.send("{\"protocol\":1,\"type\":\"list\",\"page\":" + std::to_string(page) + "}");
        pending_ = true;
        last_request_ = std::chrono::steady_clock::now();
    } catch (const std::exception& error) {
        failed_ = true;
        status_ = error.what();
    }
}
void LiveMatchesClient::watch(const std::string& id) {
    try {
        require(id.size() == 32 &&
                    std::all_of(id.begin(), id.end(),
                                [](char c) {
                                    return std::string_view("0123456789abcdef").find(c) !=
                                           std::string_view::npos;
                                }),
                "Invalid live match identifier.");
        id_ = id;
        connect();
        socket_.send(request("watch", id_) + '}');
        pending_ = true;
        last_request_ = std::chrono::steady_clock::now();
    } catch (const std::exception& error) {
        failed_ = true;
        status_ = error.what();
    }
}
void LiveMatchesClient::close() {
    socket_.disconnect();
    pending_ = false;
}
void LiveMatchesClient::tick() {
    if (failed_ || ended_)
        return;
    try {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& text : socket_.take()) {
            const auto event = parse(text);
            const auto type = event.str("type");
            if (type == "list") {
                const auto* rows = event.get("matches");
                require(rows && rows->is_array() && rows->array.size() <= 16,
                        "Invalid live match list.");
                matches_.clear();
                for (const auto& row : rows->array) {
                    LiveMatchEntry entry;
                    entry.id = row.str("id");
                    entry.stage = number(row, "stage", 31);
                    entry.frames = number(row, "frames", kMaxReplayFrames);
                    entry.viewers = number(row, "viewers", 256);
                    for (unsigned p = 0; p < 2; ++p) {
                        entry.names[p] = normalize_player_name(row.str(p ? "p2" : "p1"));
                        entry.fighters[p] = number(row, p ? "f2" : "f1", 27);
                    }
                    matches_.push_back(std::move(entry));
                }
                more_ = event.flag("more", false);
                status_ = matches_.empty() ? "No live matches are being shared."
                                           : "Choose a match to spectate.";
                pending_ = false;
            } else if (type == "watch") {
                require(event.str("id") == id_, "Unexpected live match.");
                replay_ = decode_match_replay(unhex(event.str("header")));
                require(replay_->online && !replay_->complete && replay_->inputs.size() == 1,
                        "Invalid live match header.");
                replay_->inputs.clear();
                join_frame_ = number(event, "frames", kMaxReplayFrames);
                pending_ = false;
                status_ = "Buffering confirmed match inputs...";
            } else if (type == "inputs") {
                require(replay_ && event.str("id") == id_ &&
                            number(event, "from", kMaxReplayFrames) == replay_->inputs.size(),
                        "Live match input sequence differs.");
                const auto bytes = unhex(event.str("inputs"));
                require(bytes.size() <= kLiveChunkFrames * 4 && bytes.size() % 4 == 0 &&
                            replay_->inputs.size() + bytes.size() / 4 <= kMaxReplayFrames,
                        "Live match input buffer is full.");
                for (std::size_t at = 0; at < bytes.size(); at += 4) {
                    const auto p1 = unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8);
                    const auto p2 = unsigned(bytes[at + 2]) | (unsigned(bytes[at + 3]) << 8);
                    require(p1 <= 1023 && p2 <= 1023, "Invalid live button input.");
                    replay_->inputs.push_back({std::uint16_t(p1), std::uint16_t(p2)});
                }
                ended_ = event.flag("ended", false);
                replay_->complete = event.flag("complete", false);
                replay_->final_crc = number(event, "crc", UINT32_MAX);
                require(ended_ || (!replay_->complete && !replay_->final_crc),
                        "Invalid live match completion.");
                pending_ = false;
                status_ = ended_ ? "Match ended." : "Receiving confirmed match inputs.";
            }
        }
        if (pending_ && now - last_request_ > std::chrono::seconds(15))
            throw std::runtime_error("Spectator service timed out. Refresh to reconnect.");
        if (replay_ && !pending_ && !ended_ &&
            now - last_request_ >= std::chrono::milliseconds(100)) {
            socket_.send(request("pull", id_) +
                         ",\"from\":" + std::to_string(replay_->inputs.size()) + '}');
            pending_ = true;
            last_request_ = now;
        }
        if (id_.empty() && !pending_ && socket_.active() &&
            now - last_request_ >= std::chrono::seconds(5)) {
            socket_.send("{\"protocol\":1,\"type\":\"list\",\"page\":" + std::to_string(page_) +
                         "}");
            pending_ = true;
            last_request_ = now;
        }
    } catch (const std::exception& error) {
        failed_ = true;
        status_ = error.what();
        socket_.disconnect();
    }
}
LiveMatchPublisher::LiveMatchPublisher(const MatchReplay& source) : id_(replay_unique_id()) {
    auto header = source;
    header.inputs = {{0, 0}};
    header.complete = false;
    header.final_crc = 0;
    socket_.connect(std::getenv("SEEDPC_SPECTATOR_FIXTURE")
                        ? std::getenv("SEEDPC_SPECTATOR_FIXTURE")
                        : kSpectatingUrl);
    socket_.send(request("publish", id_) +
                 ",\"header\":" + quote(hex(encode_match_replay(header))) +
                 ",\"p1\":" + quote(source.names[0]) + ",\"p2\":" + quote(source.names[1]) +
                 ",\"f1\":" + std::to_string(source.fighters[0]) +
                 ",\"f2\":" + std::to_string(source.fighters[1]) +
                 ",\"stage\":" + std::to_string(source.stage) + '}');
}
void LiveMatchPublisher::tick() {
    if (failed_)
        return;
    try {
        for (const auto& text : socket_.take()) {
            parse(text);
        }
    } catch (const std::exception&) {
        failed_ = true;
        socket_.disconnect();
    }
}
void LiveMatchPublisher::append(std::span<const std::array<std::uint16_t, 2>> inputs, bool finish,
                                bool complete, std::uint32_t crc) {
    if (failed_)
        return;
    try {
        require(inputs.size() >= sent_ && inputs.size() <= kMaxReplayFrames,
                "Invalid confirmed input history.");
        while (inputs.size() - sent_ >= kLiveChunkFrames || (finish && inputs.size() > sent_)) {
            const auto count = std::min<std::size_t>(inputs.size() - sent_, kLiveChunkFrames);
            std::vector<std::uint8_t> bytes;
            for (const auto& frame : inputs.subspan(sent_, count))
                for (auto value : frame) {
                    bytes.push_back(std::uint8_t(value));
                    bytes.push_back(std::uint8_t(value >> 8));
                }
            socket_.send(request("append", id_) + ",\"from\":" + std::to_string(sent_) +
                         ",\"inputs\":" + quote(hex(bytes)) + '}');
            sent_ += unsigned(count);
        }
        if (finish) {
            socket_.send(request("end", id_) + ",\"frames\":" + std::to_string(sent_) +
                         ",\"complete\":" + (complete ? "true" : "false") +
                         ",\"crc\":" + std::to_string(complete ? crc : 0) + '}');
            // Only after combat has finished. No viewer can delay a battle frame.
            socket_.flush(1000);
        }
    } catch (const std::exception&) {
        failed_ = true;
        socket_.disconnect();
    }
}
} // namespace seed
