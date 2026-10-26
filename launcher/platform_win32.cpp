#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <shobjidl.h>
#include <shellapi.h>
#include "platform.h"
#include <array>
#include <fstream>
#include <stdexcept>

namespace seed::launcher {
std::string utf8(const std::wstring& s) {
    if (s.empty())
        return {};
    const int length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                            nullptr, 0, nullptr, nullptr);
    if (!length)
        throw std::runtime_error("Invalid file path");
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string& s) {
    if (s.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (!length)
        throw std::runtime_error("Invalid UTF-8 file path");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        result.data(), length);
    return result;
}
std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD count =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size())
        throw std::runtime_error("Cannot locate the launcher folder");
    return std::filesystem::path(std::wstring(buffer.data(), count)).parent_path();
}
std::wstring quote_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
std::optional<std::string> pick_file(void* owner, bool bios) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Cannot open the Windows file picker");
    COMDLG_FILTERSPEC filters[] = {
        {bios ? L"GBA BIOS image" : L"GBA cartridge", bios ? L"*.bin;*.rom" : L"*.gba"},
        {L"All files", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(bios ? L"Choose your extracted GBA BIOS" : L"Choose your GBA cartridge");
    const HRESULT status = dialog->Show(static_cast<HWND>(owner));
    std::optional<std::string> result;
    if (SUCCEEDED(status)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = utf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        throw std::runtime_error("The file picker could not select a file");
    return result;
}
std::string file_hash(const std::filesystem::path& path, bool sha1) {
    struct HashHandles {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ~HashHandles() {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } handles;
    const auto checked = [](NTSTATUS result) {
        if (result < 0)
            throw std::runtime_error("File verification failed");
    };
    checked(BCryptOpenAlgorithmProvider(
        &handles.algorithm, sha1 ? BCRYPT_SHA1_ALGORITHM : BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    checked(BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0));
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read this file");
    std::array<unsigned char, 65536> block{};
    while (input) {
        input.read(reinterpret_cast<char*>(block.data()), block.size());
        const auto count = input.gcount();
        if (count)
            checked(BCryptHashData(handles.hash, block.data(), static_cast<ULONG>(count), 0));
    }
    if (!input.eof())
        throw std::runtime_error("The file could not be read completely");
    std::array<unsigned char, 32> digest{};
    const ULONG size = sha1 ? 20 : 32;
    checked(BCryptFinishHash(handles.hash, digest.data(), size, 0));
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (ULONG i = 0; i < size; ++i) {
        result += digits[digest[i] >> 4];
        result += digits[digest[i] & 15];
    }
    return result;
}
void open_file(const std::filesystem::path& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, path.parent_path().c_str(),
                  SW_SHOWNORMAL);
}
bool replace_settings_file(const std::filesystem::path& staging, const std::filesystem::path& path,
                           std::string& error) {
    if (MoveFileExW(staging.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    error = "Settings could not replace the previous file.";
    return false;
}
GameProcess::~GameProcess() {
    if (process_)
        CloseHandle(process_);
}
bool GameProcess::start(const std::filesystem::path& executable,
                        const std::vector<std::wstring>& args, const std::filesystem::path& log,
                        std::string& error) {
    if (running()) {
        error = "The game is already running.";
        return false;
    }
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& arg : args)
        command += L" " + quote_argument(arg);
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = "Cannot create the game log in the launcher folder.";
        return false;
    }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(output);
        error = "Cannot initialize game input.";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION info{};
    const bool started =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, executable.parent_path().c_str(), &startup, &info) != FALSE;
    const DWORD failure = GetLastError();
    CloseHandle(input);
    CloseHandle(output);
    if (!started) {
        error = "Could not start the game (Windows error " + std::to_string(failure) + ").";
        return false;
    }
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    return true;
}
std::optional<unsigned long> GameProcess::poll() {
    if (!process_ || WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
        return {};
    DWORD code = 1;
    GetExitCodeProcess(process_, &code);
    CloseHandle(process_);
    process_ = nullptr;
    return code;
}
} // namespace seed::launcher
