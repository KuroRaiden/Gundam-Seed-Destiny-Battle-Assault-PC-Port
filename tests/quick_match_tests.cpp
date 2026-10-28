#include "quick_match.h"
#include "online_room.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace seed;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
constexpr const char* id = "8b9f6807-49c8-42eb-ad98-bc440273aaef";
std::string event(const char* type, const std::string& rest = {}) {
    return "{\"protocol\":1,\"type\":\"" + std::string(type) + "\"" + rest + "}";
}
std::string match(const char* type, const std::string& rest = {}) {
    return event(type, ",\"match\":\"" + std::string(id) + "\"" + rest);
}
void queued(QuickMatchProtocol& p) {
    p.start("Kira \"test\"", "na", true);
    p.receive(event("socket_open"));
    const auto hello = p.take_messages();
    require(hello.size() == 1 && hello[0].find("Kira \\\"test\\\"") != std::string::npos,
            "Name was not safely quoted");
    p.receive(event("ready", ",\"waiting\":2,\"eligible\":1,\"region\":\"na\""));
    require(p.view().waiting == 2 && p.view().eligible == 1 && p.take_messages().size() == 1,
            "Queue start failed");
    p.receive(event("searching"));
    p.take_actions();
}
void protocol_tests() {
    QuickMatchClient failed_client;
    failed_client.failed("Example match-end failure");
    failed_client.observe("Kira", "any");
    require(failed_client.view().phase == QuickMatchPhase::Error &&
                failed_client.view().status == "Example match-end failure",
            "Opening the connection screen erased the match-end failure");
    for (const auto* address :
         {"https://example.com/v1/connect",
          "http://seedpc-matchmaking.lord19uga.workers.dev/v1/connect",
          "http://127.0.0.1:8788@other.example/v1/connect", "http://127.0.0.1:8788/wrong"}) {
        MatchmakingSocket socket;
        bool rejected = false;
        try {
            socket.connect(address);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Unexpected service endpoint accepted");
    }
    QuickMatchProtocol host;
    queued(host);
    host.receive(match("match_found", ",\"role\":\"host\",\"opponent\":\"Athrun\""));
    require(host.take_messages().size() == 1 && host.view().opponent == "Athrun",
            "Offer acceptance failed");
    host.receive(match("prepare"));
    host.receive(match("prepare"));
    auto actions = host.take_actions();
    require(actions.size() == 1 && actions[0].kind == QuickRoomAction::Host,
            "Duplicate host creation");
    JoinCode key;
    key.public_endpoint = {0x0100007f, 45678};
    key.local_port = 45678;
    key.token[0] = 42;
    const auto code = encode_join_code(key);
    host.publish(code);
    host.publish(code);
    require(host.take_messages().size() == 1, "Invitation publication was duplicated");
    host.connected();
    host.connected();
    require(host.take_messages().size() == 1, "Connected confirmation was duplicated");
    host.receive(event("socket_error", ",\"message\":\"test disconnect\""));
    require(host.view().phase == QuickMatchPhase::Connected && host.take_actions().empty(),
            "Service loss interrupted connected play");
    host.receive(event("searching", ",\"reason\":\"opponent_left\""));
    host.receive(match("match_found", ",\"role\":\"host\",\"opponent\":\"Another player\""));
    require(host.view().phase == QuickMatchPhase::Connected && host.take_actions().empty() &&
                host.take_messages().empty(),
            "A late service requeue reused an established room");
    QuickMatchProtocol guest;
    queued(guest);
    guest.receive(match("match_found", ",\"role\":\"guest\",\"opponent\":\"Kira\""));
    guest.take_messages();
    guest.receive(match("preparing"));
    guest.receive(match("connect", ",\"code\":\"" + code + "\""));
    actions = guest.take_actions();
    require(actions.size() == 1 && actions[0].kind == QuickRoomAction::Join &&
                actions[0].code == code,
            "Private invitation handoff failed");
    guest.receive(event("searching", ",\"reason\":\"opponent_left\""));
    require(guest.view().phase == QuickMatchPhase::Searching && guest.take_actions().size() == 1,
            "Disconnect did not clean up a pending room");
    for (const auto& bad :
         {event("count", ",\"region\":\"na\",\"waiting\":-1,\"eligible\":0"),
          event("count", ",\"region\":\"na\",\"waiting\":1e100,\"eligible\":0"),
          event("count", ",\"region\":\"na\",\"waiting\":2.5,\"eligible\":0"),
          event("count", ",\"region\":\"eu\",\"waiting\":0,\"eligible\":0"),
          std::string("{\"protocol\":2,\"type\":\"ready\"}"), std::string("bad JSON")}) {
        QuickMatchProtocol bad_peer;
        queued(bad_peer);
        bad_peer.receive(bad);
        require(bad_peer.view().phase == QuickMatchPhase::Error && bad_peer.release_socket(),
                "Invalid service response was accepted");
    }
    QuickMatchProtocol stale;
    queued(stale);
    stale.receive(match("prepare"));
    require(stale.view().phase == QuickMatchPhase::Error, "Unassigned room was accepted");
    QuickMatchProtocol complete;
    queued(complete);
    complete.receive(match("match_found", ",\"role\":\"host\",\"opponent\":\"Athrun\""));
    complete.receive(match("prepare"));
    complete.take_actions();
    complete.connected();
    complete.receive(match("handoff"));
    require(complete.release_socket() && complete.view().phase == QuickMatchPhase::Connected,
            "Handoff did not release service connection");
    std::cout << "PASS: queue counts, validation, offers, invitation privacy, idempotent handoff, "
                 "requeue and connected-room isolation.\n";
}
void live(const std::string& url) {
    require(SDL_Init(0), "SDL initialization failed");
    QuickMatchClient clients[2];
    OnlineRoom rooms[2];
    LocalVsSelection selections[2];
    for (unsigned seat = 0; seat < 2; ++seat) {
        selections[seat].jp_layout = true;
        selections[seat].stage_enabled = true;
        clients[seat].fixture(url, "127.0.0.1");
    }
    auto pump = [&] {
        for (unsigned seat = 0; seat < 2; ++seat) {
            clients[seat].tick(rooms[seat], selections[seat]);
            if (rooms[seat].active())
                rooms[seat].tick(selections[seat], 0);
            require(clients[seat].view().phase != QuickMatchPhase::Error,
                    clients[seat].view().status.c_str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };
    auto until = [&](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (!condition()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Native matchmaking qualification timed out");
            pump();
        }
    };
    clients[0].search("Kira", "any");
    clients[1].observe("Athrun", "any");
    until([&] {
        return clients[0].view().phase == QuickMatchPhase::Searching &&
               clients[1].view().waiting >= 1;
    });
    std::cout << "PASS: observing client received a live waiting-player count.\n";
    clients[1].search("Athrun", "any");
    until([&] {
        return rooms[0].connected() && rooms[1].connected() &&
               clients[0].view().phase == QuickMatchPhase::Connected &&
               clients[1].view().phase == QuickMatchPhase::Connected;
    });
    require(rooms[0].seat() != rooms[1].seat() && rooms[0].names() == rooms[1].names(),
            "Native room roles or player names differ");
    for (unsigned n = 0; n < 50; ++n)
        pump();
    const auto cancel_began = std::chrono::steady_clock::now();
    clients[0].cancel();
    clients[1].cancel();
    require(std::chrono::steady_clock::now() - cancel_began < std::chrono::milliseconds(100),
            "Service cancellation blocked game thread");
    for (unsigned n = 0; n < 50; ++n)
        pump();
    require(rooms[0].connected() && rooms[1].connected(),
            "Closing matchmaking interrupted the P2P room");
    rooms[0].close();
    rooms[1].close();
    clients[0].search("Kira", "any");
    until([&] { return clients[0].view().phase == QuickMatchPhase::Searching; });
    clients[0].cancel();
    require(clients[0].view().phase == QuickMatchPhase::Offline, "Queue cancel failed");
    std::cout
        << "PASS: two native Windows clients used WebSocket matchmaking to create a real UDP room, "
           "with matching names, independent service teardown and nonblocking cancellation.\n";
    SDL_Quit();
}
int main(int argc, char** argv) {
    try {
        protocol_tests();
        if (argc == 3 && std::string(argv[1]) == "--live")
            live(argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
