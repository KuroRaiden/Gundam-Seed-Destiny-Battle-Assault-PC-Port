#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "tcp_debug_server.h"
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstdio>
int main() {
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    try {
        gbarecomp::debug::TcpDebugServer server;
        gbarecomp::debug::TcpDebugServer::Context context{};
        std::thread worker([&] { server.run(24877, context); });
        SOCKET client = INVALID_SOCKET;
        for (unsigned i = 0; i < 100; ++i) {
            client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(24877);
            target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(client, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0)
                break;
            closesocket(client);
            client = INVALID_SOCKET;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (client == INVALID_SOCKET) {
            server.stop();
            worker.join();
            throw std::runtime_error("No test server");
        }
        // Keep the client open and idle. A game must still be able to return.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto start = std::chrono::steady_clock::now();
        server.stop();
        worker.join();
        closesocket(client);
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2))
            throw std::runtime_error("Debug shutdown stalled");
        std::puts("PASS: native debug shutdown with an idle connected client.");
        WSACleanup();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        WSACleanup();
        return 1;
    }
}
