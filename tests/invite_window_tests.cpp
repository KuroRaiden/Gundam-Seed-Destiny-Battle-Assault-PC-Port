#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "invite_window.h"
#include "invite_code.h"
#include <cstdio>
#include <stdexcept>
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"SeedInviteTest";
        check(RegisterClassW(&c), "register window class");
        const auto window = CreateWindowW(c.lpszClassName, L"Invitation test", WS_OVERLAPPED, 0, 0,
                                          100, 100, nullptr, nullptr, c.hInstance, nullptr);
        check(window, "create test window");
        seed::attach_invite_window(window);
        wchar_t image[32768];
        GetModuleFileNameW(nullptr, image, 32768);
        seed::JoinCode value;
        value.public_endpoint = {0x0100007f, 4567};
        value.local_port = 4567;
        value.token[0] = 34;
        const auto code = seed::encode_join_code(value);
        std::string got;
        check(seed::forward_game_invite(image, seed::invitation_link(value)) ==
                  seed::InviteDelivery::Delivered,
              "existing game delivery");
        check(seed::take_game_invite(got) && got == code, "canonical delivered code");
        seed::set_invite_busy(true);
        check(seed::forward_game_invite(image, code) == seed::InviteDelivery::Queued,
              "busy match queue");
        check(!seed::take_game_invite(got), "busy match consumed invitation");
        seed::set_invite_busy(false);
        check(seed::take_game_invite(got) && got == code, "queued invite lost");
        char invalid[] = "SEED1-invalid";
        COPYDATASTRUCT data{0x53444931, sizeof(invalid), invalid};
        check(!SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data)),
              "malformed invitation accepted");
        check(!seed::take_game_invite(got), "malformed invite queued");
        seed::detach_invite_window(window);
        DestroyWindow(window);
        check(seed::forward_game_invite(image, code) == seed::InviteDelivery::NoGame,
              "missing game not detected");
        std::puts("PASS: invitation link delivery, canonical code, queued combat invitation and "
                  "malformed-message rejection; no registry changes.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
