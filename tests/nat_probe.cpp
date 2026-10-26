#include "udp_transport.h"
#include <SDL3/SDL.h>
#include <cstdio>
int main() {
    seed::UdpTransport transport(0, "", 0, {});
    transport.enable_nat();
    const auto start = SDL_GetTicks();
    seed::NatInfo info;
    do {
        transport.poll();
        info = transport.discovery();
        SDL_Delay(10);
    } while (SDL_GetTicks() - start < 7000 && (info.searching || !info.autopunch_reachable));
    std::printf("{\"public_endpoint_discovered\":%s,\"autopunch_server_reachable\":%s,\"lan_"
                "available\":%s}\n",
                info.public_endpoint ? "true" : "false",
                info.autopunch_reachable ? "true" : "false", info.lan_ipv4 ? "true" : "false");
    return info.public_endpoint && info.autopunch_reachable ? 0 : 2;
}
