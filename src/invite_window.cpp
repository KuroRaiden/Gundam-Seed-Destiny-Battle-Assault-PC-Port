#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "invite_window.h"
#include "invite_code.h"
#include <atomic>
#include <cstring>
#include <mutex>
namespace seed {
namespace {
constexpr ULONG_PTR signature = 0x53444931;
constexpr wchar_t property[] = L"GundamSeed.Invite.PreviousProcedure";
std::mutex queue_mutex;
std::string pending;
std::atomic<bool> busy{false};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto previous = reinterpret_cast<WNDPROC>(GetPropW(window, property));
    if (message == WM_COPYDATA) {
        const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (copy && copy->dwData == signature && copy->lpData && copy->cbData > 1 &&
            copy->cbData <= 513) {
            const auto* text = static_cast<const char*>(copy->lpData);
            if (text[copy->cbData - 1] || std::memchr(text, 0, copy->cbData - 1))
                return 0;
            try {
                queue_game_invite(std::string(text, copy->cbData - 1));
                return busy.load() ? 2 : 1;
            } catch (...) {
                return 0;
            }
        }
    }
    return previous ? CallWindowProcW(previous, window, message, wp, lp)
                    : DefWindowProcW(window, message, wp, lp);
}
struct Search {
    std::filesystem::path expected;
    HWND window = nullptr;
};
BOOL CALLBACK find_window(HWND window, LPARAM context) {
    auto& search = *reinterpret_cast<Search*>(context);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return TRUE;
    wchar_t image[32768];
    DWORD count = 32768;
    const bool found = QueryFullProcessImageNameW(process, 0, image, &count) != FALSE;
    CloseHandle(process);
    std::error_code error;
    if (found && std::filesystem::equivalent(
                     search.expected, std::filesystem::path(std::wstring(image, count)), error)) {
        if (GetPropW(window, property)) {
            search.window = window;
            return FALSE;
        }
        if (!search.window)
            search.window = window;
    }
    return TRUE;
}
} // namespace
void queue_game_invite(const std::string& text) {
    const auto code = encode_join_code(decode_join_code(text));
    std::lock_guard guard(queue_mutex);
    pending = code;
}
bool take_game_invite(std::string& code) {
    if (busy.load())
        return false;
    std::lock_guard guard(queue_mutex);
    if (pending.empty())
        return false;
    code = std::move(pending);
    pending.clear();
    return true;
}
void set_invite_busy(bool value) {
    busy.store(value);
}
void attach_invite_window(void* native) {
    const auto window = static_cast<HWND>(native);
    if (!window || GetPropW(window, property))
        return;
    const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!SetPropW(window, property, reinterpret_cast<HANDLE>(previous)))
        return;
    SetLastError(0);
    if (!SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure)) &&
        GetLastError())
        RemovePropW(window, property);
}
void detach_invite_window(void* native) {
    const auto window = static_cast<HWND>(native);
    if (!window)
        return;
    if (auto previous = GetPropW(window, property)) {
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        RemovePropW(window, property);
    }
}
InviteDelivery forward_game_invite(const std::filesystem::path& game, const std::string& text) {
    const auto code = encode_join_code(decode_join_code(text));
    Search search{game};
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
    if (!search.window)
        return InviteDelivery::NoGame;
    if (!GetPropW(search.window, property))
        return InviteDelivery::Busy;
    COPYDATASTRUCT copy{signature, DWORD(code.size() + 1), const_cast<char*>(code.c_str())};
    DWORD_PTR reply = 0;
    if (!SendMessageTimeoutW(search.window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 750, &reply) ||
        !reply)
        return InviteDelivery::Busy;
    ShowWindow(search.window, SW_RESTORE);
    SetForegroundWindow(search.window);
    return reply == 2 ? InviteDelivery::Queued : InviteDelivery::Delivered;
}
bool register_invite_launcher(const std::filesystem::path& launcher, std::string& error) {
    if (!std::filesystem::is_regular_file(launcher)) {
        error = "The launcher is missing from this folder.";
        return false;
    }
    auto write = [&](const wchar_t* key, const wchar_t* name, const std::wstring& value) {
        HKEY handle = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle,
                            nullptr) != ERROR_SUCCESS)
            return false;
        const auto result =
            RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                           DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(handle);
        return result == ERROR_SUCCESS;
    };
    const auto command = L"\"" + launcher.wstring() + L"\" --join \"%1\"";
    if (!write(L"Software\\Classes\\seedpc", nullptr, L"URL:Gundam SEED invitation") ||
        !write(L"Software\\Classes\\seedpc", L"URL Protocol", L"") ||
        !write(L"Software\\Classes\\seedpc\\shell\\open\\command", nullptr, command)) {
        error = "Windows could not enable invitation links for this copy of the game.";
        return false;
    }
    error = "Invitation links now open with this copy of the game.";
    return true;
}
} // namespace seed
