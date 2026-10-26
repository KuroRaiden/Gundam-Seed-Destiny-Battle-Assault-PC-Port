#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include "online_room.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstring>
#include <stdexcept>
namespace seed {
namespace {
std::uint64_t now() {
    return SDL_GetTicks();
}
void put32(std::uint8_t* p, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = std::uint8_t(n >> (8 * i));
}
std::uint32_t get32(const std::uint8_t* p) {
    return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}
void random_bytes(void* data, unsigned size) {
    if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(data), size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) <
        0)
        throw std::runtime_error("Cannot create a random room key");
}
std::vector<std::uint8_t> packet(unsigned kind, unsigned seat, std::uint32_t sequence,
                                 std::uint32_t epoch) {
    std::vector<std::uint8_t> p(32);
    put32(p.data(), kRoomMagic);
    put32(p.data() + 4, kRoomProtocol);
    p[8] = std::uint8_t(kind);
    p[9] = std::uint8_t(seat);
    put32(p.data() + 16, sequence);
    put32(p.data() + 20, epoch);
    return p;
}
bool header(const std::vector<std::uint8_t>& p, unsigned remote) {
    return p.size() > 9 && p.size() == (p[8] == 2 ? 32 + kPlayerNameBytes + 1 : 32) &&
           get32(p.data()) == kRoomMagic && get32(p.data() + 4) == kRoomProtocol && p[9] == remote;
}
} // namespace
bool acknowledge_room_start(UdpTransport& transport, const std::vector<std::uint8_t>& p,
                            const NetConnection& c, const std::uint8_t* fighters) {
    if (!header(p, 1 - c.seat) || p[8] != 3 || get32(p.data() + 20) != c.epoch ||
        p[24] + 1 != fighters[0] || p[25] + 1 != fighters[1])
        return false;
    auto ack = packet(4, c.seat, 0, c.epoch);
    ack[24] = p[24];
    ack[25] = p[25];
    return transport.send_control(ack);
}
void OnlineRoom::host(std::uint16_t port, const std::string& advertised_host, NatOptions options,
                      const std::string& name) {
    close();
    UdpTransport::Token token{};
    random_bytes(token.data(), unsigned(token.size()));
    transport_ = std::make_unique<UdpTransport>(port, "", 0, token);
    seat_ = 0;
    names_[0] = normalize_player_name(name);
    created_ = last_receive_ = now();
    status_ = "Creating your join code...";
    if (!advertised_host.empty()) {
        // Explicit localhost fixture only; the player UI never requests an IP.
        JoinCode value;
        value.local_port = transport_->local_port();
        value.public_endpoint.port = value.local_port;
        value.token = token;
        if (inet_pton(AF_INET, advertised_host.c_str(), &value.public_endpoint.ipv4) != 1)
            throw std::runtime_error("The developer host fixture must be an IPv4 address");
        code_ = encode_join_code(value);
        invitation_ = invitation_link(value);
        status_ = "Code ready. Share it with your opponent.";
    } else
        transport_->enable_nat(std::move(options));
}
void OnlineRoom::join(const std::string& invitation, NatOptions options, const std::string& name) {
    close();
    const auto value = decode_join_code(invitation);
    char host[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &value.public_endpoint.ipv4, host, sizeof(host));
    transport_ = std::make_unique<UdpTransport>(0, host, value.public_endpoint.port, value.token);
    seat_ = 1;
    names_[1] = normalize_player_name(name);
    if (value.lan_ipv4 && NatTraversal::same_lan(value.lan_ipv4))
        transport_->add_candidate({value.lan_ipv4, value.local_port});
    if (value.autopunch) {
        transport_->enable_nat(std::move(options));
        transport_->punch_target(value.public_endpoint, value.local_port);
    }
    code_ = encode_join_code(value);
    invitation_ = invitation_link(value);
    created_ = last_receive_ = now();
    status_ = "Finding your opponent...";
}
void OnlineRoom::send(unsigned kind, const LocalVsSelection* selection) {
    if (!transport_)
        return;
    auto p = packet(kind, seat_, ++sequence_, epoch_);
    if (selection) {
        const auto& s = selection->seats[seat_];
        p[10] = s.cursor;
        p[11] = s.fighter;
        p[12] = s.ready;
    }
    if (kind == 2) {
        p.resize(32 + kPlayerNameBytes + 1);
        std::copy(names_[seat_].begin(), names_[seat_].end(), p.begin() + 32);
    }
    p[24] = fighters_[0];
    p[25] = fighters_[1];
    transport_->send_control(p);
    last_send_ = now();
}
void OnlineRoom::close() {
    reset(true);
}
void OnlineRoom::reset(bool notify_peer) {
    if (transport_ && notify_peer)
        send(5);
    transport_.reset();
    inputs_ = {};
    code_.clear();
    invitation_.clear();
    connected_ = launch_ = pending_ = remote_seen_ = false;
    sequence_ = remote_sequence_ = epoch_ = 0;
    last_receive_ = last_send_ = created_ = 0;
    fighters_ = {};
    names_ = {"Player 1", "Player 2"};
    failed_ = false;
}
void OnlineRoom::tick(LocalVsSelection& selection, std::uint16_t local_input) {
    if (!transport_ || launch_)
        return;
    transport_->poll();
    if (!seat_ && code_.empty()) {
        const auto discovery = transport_->discovery();
        if (discovery.public_endpoint || !discovery.searching) {
            JoinCode value;
            value.public_endpoint = discovery.public_endpoint;
            value.lan_ipv4 = discovery.lan_ipv4;
            value.local_port = transport_->local_port();
            value.token = transport_->borrow(0, 0).token;
            value.internet = bool(discovery.public_endpoint);
            value.autopunch = value.internet;
            if (!value.public_endpoint)
                value.public_endpoint = {value.lan_ipv4, value.local_port};
            if (!value.public_endpoint) {
                close();
                failed_ = true;
                status_ =
                    "No network connection was found. Connect to a network and create a new code.";
                selection.cancelled = true;
                return;
            }
            code_ = encode_join_code(value);
            invitation_ = invitation_link(value);
            status_ = value.internet ? "Code ready. Share it with your opponent."
                                     : "LAN code ready. Internet discovery is unavailable.";
        }
    }
    for (const auto& p : transport_->take_control()) {
        if (!header(p, 1 - seat_))
            continue;
        if (p[8] == 5) {
            close();
            failed_ = true;
            status_ = "Opponent left the room.";
            selection.cancelled = true;
            return;
        }
        if (p[8] == 2) {
            const auto* name = reinterpret_cast<const char*>(p.data() + 32);
            const auto* end = static_cast<const char*>(std::memchr(name, 0, kPlayerNameBytes + 1));
            if (!end ||
                !std::all_of(end, name + kPlayerNameBytes + 1, [](char c) { return c == 0; }) ||
                !valid_player_name(std::string(name, end)))
                continue;
            const auto seq = get32(p.data() + 16);
            if (remote_seen_ && std::int32_t(seq - remote_sequence_) <= 0)
                continue;
            const auto cursor = p[10], fighter = p[11];
            if (cursor >= kJpPortraits.size() ||
                (kJpPortraits[cursor] == 255 && cursor != 11 && cursor != 12) ||
                (fighter != 255 && !selection.supported(fighter)) || p[12] > 1 ||
                (p[12] && fighter == 255))
                continue;
            names_[1 - seat_] = std::string(name, end);
            remote_sequence_ = seq;
            remote_seen_ = true;
            last_receive_ = now();
            connected_ = true;
            selection.seats[1 - seat_] = {cursor, fighter, bool(p[12])};
            status_ = seat_ ? "Connected. You control Player 2 with your Player 1 bindings."
                            : "Connected. You control Player 1.";
        } else if (p[8] == 3 && seat_ == 1 && connected_ && selection.seats[0].ready &&
                   selection.seats[1].ready) {
            if (p[24] != selection.seats[0].fighter || p[25] != selection.seats[1].fighter ||
                !get32(p.data() + 20))
                continue;
            epoch_ = get32(p.data() + 20);
            fighters_ = {p[24], p[25]};
            send(4);
            pending_ = launch_ = true;
            last_receive_ = now();
        } else if (p[8] == 4 && seat_ == 0 && pending_ && get32(p.data() + 20) == epoch_ &&
                   p[24] == fighters_[0] && p[25] == fighters_[1]) {
            launch_ = true;
            last_receive_ = now();
        }
    }
    if (now() - last_receive_ > 15000 && (connected_ || seat_ == 1)) {
        close();
        failed_ = true;
        status_ = "Could not reach your opponent. Ask the host for a fresh code; this network may "
                  "block direct connections.";
        selection.cancelled = true;
        return;
    }
    if (launch_)
        return;
    if (connected_ && !pending_) {
        inputs_.advance(seat_ ? 0 : local_input, seat_ ? local_input : 0);
        selection.advance(inputs_);
        selection.launch = false;
        selection.ready_frames = 0;
        if (selection.cancelled) {
            close();
            return;
        }
        if (!seat_ && selection.seats[0].ready && selection.seats[1].ready) {
            random_bytes(&epoch_, sizeof(epoch_));
            if (!epoch_)
                epoch_ = 1;
            fighters_ = {selection.seats[0].fighter, selection.seats[1].fighter};
            pending_ = true;
            last_send_ = 0;
        }
    } else
        inputs_.advance(0, 0);
    if (now() - last_send_ >= 80) {
        // The guest may have confirmed before this host published its own
        // ready edge. Keep repeating the ready snapshot with the start offer;
        // otherwise a lost/unpublished selection packet can strand both peers.
        if (pending_ && !seat_)
            send(2, &selection);
        send(pending_ && !seat_ ? 3 : 2, &selection);
    }
}
NetConnection OnlineRoom::connection() {
    if (!transport_ || !launch_)
        throw std::runtime_error("Room is not ready for combat");
    transport_->set_game_epoch(epoch_);
    return transport_->borrow(seat_, epoch_);
}
void OnlineRoom::match_returned() {
    // Each battle creates a fresh room to exclude old selection packets and
    // stale native input traffic, including a lost leave/disconnect notice.
    // Native combat already negotiated its common finish and exchanged final
    // checksums. A generic room-leave notice here can race the other peer's
    // final acknowledgement window and incorrectly turn success into failure.
    reset(false);
    status_ = "Match ended. Create or join a new room to play again.";
}
} // namespace seed
