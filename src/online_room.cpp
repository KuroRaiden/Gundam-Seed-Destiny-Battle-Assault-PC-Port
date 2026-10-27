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
    std::vector<std::uint8_t> p(kind == 2 ? 32 + kPlayerNameBytes + 1 + kMsPaletteBytes
                                : (kind == 3 || kind == 4) ? 32 + 2 * kMsPaletteBytes
                                                           : 32);
    put32(p.data(), kRoomMagic);
    put32(p.data() + 4, kRoomProtocol);
    p[8] = std::uint8_t(kind);
    p[9] = std::uint8_t(seat);
    put32(p.data() + 16, sequence);
    put32(p.data() + 20, epoch);
    return p;
}
bool header(const std::vector<std::uint8_t>& p, unsigned remote) {
    return p.size() > 9 &&
           p.size() == (p[8] == 2                  ? 32 + kPlayerNameBytes + 1 + kMsPaletteBytes
                        : (p[8] == 3 || p[8] == 4) ? 32 + 2 * kMsPaletteBytes
                                                   : 32) &&
           get32(p.data()) == kRoomMagic && get32(p.data() + 4) == kRoomProtocol && p[9] == remote;
}
} // namespace
bool acknowledge_room_start(UdpTransport& transport, const std::vector<std::uint8_t>& p,
                            const NetConnection& c, const std::uint8_t* fighters,
                            const std::uint8_t* colors, const CustomPalette* palettes) {
    if (!header(p, 1 - c.seat) || p[8] != 3 || get32(p.data() + 20) != c.epoch ||
        !valid_jp_color(p[24], p[26]) || !valid_jp_color(p[25], p[27]))
        return false;
    for (unsigned seat = 0; seat < 2; ++seat) {
        const auto choice = jp_combat_choice(p[24 + seat], p[26 + seat]);
        CustomPalette custom;
        if (!decode_palette(p.data() + 32 + seat * kMsPaletteBytes, p[28 + seat], custom) ||
            custom != palettes[seat] || choice.fighter != fighters[seat] ||
            choice.palette != colors[seat])
            return false;
    }
    auto ack = packet(4, c.seat, 0, c.epoch);
    ack[24] = p[24];
    ack[25] = p[25];
    ack[26] = p[26];
    ack[27] = p[27];
    std::copy(p.begin() + 28, p.end(), ack.begin() + 28);
    return transport.send_control(ack);
}
bool acknowledge_match_finish(UdpTransport& transport, const std::vector<std::uint8_t>& p,
                              const NetConnection& connection, std::uint32_t final_crc) {
    if (p.size() != kCombatControlPacketBytes || get32(p.data()) != kCombatControlMagic ||
        get32(p.data() + 4) != kRoomProtocol || p[8] != 3 || p[9] != 1 - connection.seat ||
        get32(p.data() + 12) != connection.epoch || get32(p.data() + 16) != final_crc)
        return false;
    auto ack = p;
    ack[8] = 4;
    ack[9] = std::uint8_t(connection.seat);
    return transport.send_control(ack);
}
void OnlineRoom::host(std::uint16_t port, const std::string& advertised_host, NatOptions options,
                      const std::string& name) {
    close();
    UdpTransport::Token token{};
    random_bytes(token.data(), unsigned(token.size()));
    transport_ = std::make_unique<UdpTransport>(port, "", 0, token);
    seat_ = 0;
    random_bytes(&epoch_, sizeof(epoch_));
    if (!epoch_)
        epoch_ = 1;
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
        p[13] = s.color;
        p[14] = s.palette.enabled;
        if (kind == 2)
            encode_palette(p.data() + 32 + kPlayerNameBytes + 1, s.palette);
    }
    if (kind == 2) {
        std::copy(names_[seat_].begin(), names_[seat_].end(), p.begin() + 32);
    }
    p[24] = fighters_[0];
    p[25] = fighters_[1];
    p[26] = colors_[0];
    p[27] = colors_[1];
    if (kind == 3 || kind == 4)
        for (unsigned seat = 0; seat < 2; ++seat) {
            p[28 + seat] = palettes_[seat].enabled;
            encode_palette(p.data() + 32 + seat * kMsPaletteBytes, palettes_[seat]);
        }
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
    awaiting_peer_ = release_guard_ = completed_ = false;
    completed_epoch_ = completed_crc_ = 0;
    completed_matches_ = 0;
    fighters_ = {};
    colors_ = {};
    palettes_ = {};
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
        if (completed_ &&
            acknowledge_match_finish(*transport_, p, transport_->borrow(seat_, completed_epoch_),
                                     completed_crc_))
            continue;
        if (!header(p, 1 - seat_))
            continue;
        if (p[8] == 5) {
            const auto leave_epoch = get32(p.data() + 20);
            if (epoch_ && leave_epoch != epoch_ &&
                !(awaiting_peer_ && completed_ && leave_epoch == completed_epoch_))
                continue;
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
            CustomPalette custom;
            if (!decode_palette(p.data() + 32 + kPlayerNameBytes + 1, p[14], custom))
                continue;
            const auto cursor = p[10], fighter = p[11];
            if (cursor >= kJpPortraits.size() ||
                (kJpPortraits[cursor] == 255 && cursor != 11 && cursor != 12) ||
                (fighter != 255 && !selection.supported(fighter)) || p[12] > 1 ||
                (p[12] && fighter == 255) ||
                (fighter == 255 ? p[13] != 0 : !valid_jp_color(fighter, p[13])))
                continue;
            const auto incoming_epoch = get32(p.data() + 20);
            if (!incoming_epoch || (epoch_ && incoming_epoch != epoch_) || (!epoch_ && seat_ != 1))
                continue;
            if (!epoch_)
                epoch_ = incoming_epoch;
            awaiting_peer_ = false;
            names_[1 - seat_] = std::string(name, end);
            remote_sequence_ = seq;
            remote_seen_ = true;
            last_receive_ = now();
            connected_ = true;
            selection.seats[1 - seat_] = {cursor, fighter, bool(p[12]), p[13], custom};
            status_ = seat_ ? "Connected. You control Player 2 with your Player 1 bindings."
                            : "Connected. You control Player 1.";
        } else if (p[8] == 3 && seat_ == 1 && connected_ && selection.seats[0].ready &&
                   selection.seats[1].ready) {
            if (p[24] != selection.seats[0].fighter || p[25] != selection.seats[1].fighter ||
                p[26] != selection.seats[0].color || p[27] != selection.seats[1].color ||
                get32(p.data() + 20) != epoch_ || awaiting_peer_)
                continue;
            std::array<CustomPalette, 2> custom;
            if (!decode_palette(p.data() + 32, p[28], custom[0]) ||
                !decode_palette(p.data() + 32 + kMsPaletteBytes, p[29], custom[1]) ||
                custom[0] != selection.seats[0].palette || custom[1] != selection.seats[1].palette)
                continue;
            palettes_ = custom;
            epoch_ = get32(p.data() + 20);
            fighters_ = {p[24], p[25]};
            colors_ = {p[26], p[27]};
            send(4);
            pending_ = launch_ = true;
            last_receive_ = now();
        } else if (p[8] == 4 && seat_ == 0 && pending_ && get32(p.data() + 20) == epoch_ &&
                   p[24] == fighters_[0] && p[25] == fighters_[1] && p[26] == colors_[0] &&
                   p[27] == colors_[1]) {
            CustomPalette first, second;
            if (!decode_palette(p.data() + 32, p[28], first) ||
                !decode_palette(p.data() + 32 + kMsPaletteBytes, p[29], second) ||
                first != palettes_[0] || second != palettes_[1])
                continue;
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
        if (release_guard_) {
            if (!local_input)
                release_guard_ = false;
            else
                local_input = 0;
        }
        inputs_.advance(seat_ ? 0 : local_input, seat_ ? local_input : 0);
        selection.local_seat_mask = 1u << seat_;
        selection.advance(inputs_);
        selection.launch = false;
        selection.ready_frames = 0;
        if (selection.cancelled) {
            close();
            return;
        }
        if (!seat_ && !awaiting_peer_ && selection.seats[0].ready && selection.seats[1].ready) {
            fighters_ = {selection.seats[0].fighter, selection.seats[1].fighter};
            colors_ = {selection.seats[0].color, selection.seats[1].color};
            palettes_ = {selection.seats[0].palette, selection.seats[1].palette};
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
void OnlineRoom::match_returned(LocalVsSelection& selection, std::uint32_t final_crc) {
    if (!transport_ || !launch_)
        throw std::runtime_error("Cannot return a room without a completed match");
    completed_epoch_ = epoch_;
    completed_crc_ = final_crc;
    completed_ = true;
    ++completed_matches_;
    epoch_ = next_match_epoch(epoch_);
    // Keep the room key, pinned endpoint, socket, names and sequence numbers.
    // Selection/start traffic is scoped to the new epoch; Gekko data from the
    // previous battle is discarded before the next native session borrows it.
    transport_->set_game_epoch(0);
    launch_ = pending_ = false;
    awaiting_peer_ = release_guard_ = true;
    last_receive_ = now();
    last_send_ = 0;
    inputs_ = {};
    selection.cancelled = selection.launch = false;
    selection.ready_frames = 0;
    for (auto& seat : selection.seats)
        seat.ready = false;
    status_ = "Rematch ready. Choose suits/colors and press A. B leaves the room.";
}
} // namespace seed
