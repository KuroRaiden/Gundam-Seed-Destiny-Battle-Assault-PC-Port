#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include "update.h"
#include "platform.h"
#include "mini_json.h"
#include "launcher_version.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <thread>

namespace seed::launcher {
namespace {
constexpr char asset_prefix[] = "GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-";
constexpr char repository[] =
    "https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/";
constexpr std::uint64_t max_archive = 256ull * 1024 * 1024;
using Json = gbarecomp::json::Value;
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Version {
    unsigned major, minor, patch;
    int channel;
    unsigned revision;
    auto operator<=>(const Version&) const = default;
};
Version version(const std::string& value) {
    static const std::regex pattern(
        R"(^v?(\d{1,5})\.(\d{1,5})\.(\d{1,5})(?:-(dev|alpha|beta|rc|fixed)(?:[.-]?(\d{1,5}))?)?$)");
    std::smatch found;
    require(std::regex_match(value, found, pattern), "Unsupported release version: " + value);
    const auto suffix = found[4].str();
    int channel = suffix == "fixed"   ? 1
                  : suffix == "rc"    ? -1
                  : suffix == "beta"  ? -2
                  : suffix == "alpha" ? -3
                  : suffix == "dev"   ? -4
                                      : 0;
    return {unsigned(std::stoul(found[1])), unsigned(std::stoul(found[2])),
            unsigned(std::stoul(found[3])), channel,
            found[5].matched ? unsigned(std::stoul(found[5])) : 0};
}
bool digest_valid(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
struct Internet {
    HINTERNET value = nullptr;
    ~Internet() {
        if (value)
            WinHttpCloseHandle(value);
    }
    operator HINTERNET() const {
        return value;
    }
};
std::wstring header(HINTERNET request, DWORD which) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, which, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, nullptr);
    if (!bytes)
        return {};
    std::wstring result(bytes / sizeof(wchar_t), 0);
    require(WinHttpQueryHeaders(request, which, WINHTTP_HEADER_NAME_BY_INDEX, result.data(), &bytes,
                                nullptr),
            "Cannot read download response.");
    result.resize(bytes / sizeof(wchar_t));
    while (!result.empty() && !result.back())
        result.pop_back();
    return result;
}
void fetch(std::string url, bool asset, std::uint64_t limit, const std::atomic_bool& cancelled,
           const std::function<void(const char*, unsigned)>& consume) {
    Internet session{WinHttpOpen(L"GundamSeedPC-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    require(session.value, "Windows could not initialize HTTPS.");
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 15000);
    for (unsigned redirects = 0; redirects <= 5; ++redirects) {
        require(!cancelled, "Update cancelled.");
        require(allowed_update_url(url, redirects != 0 && asset),
                "Unexpected update download host.");
        auto address = wide(url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
        require(WinHttpCrackUrl(address.c_str(), DWORD(address.size()), 0, &parts),
                "Invalid update URL.");
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Internet connection{WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
        require(connection.value, "Cannot connect to GitHub.");
        Internet request{WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE)};
        require(request.value, "Cannot create update request.");
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        require(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)),
                "Cannot restrict update redirects.");
        DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon));
        const wchar_t* accept = asset ? L"Accept: application/octet-stream\r\n"
                                      : L"Accept: application/vnd.github+json\r\n";
        require(WinHttpSendRequest(request, accept, DWORD(-1), nullptr, 0, 0, 0) &&
                    WinHttpReceiveResponse(request, nullptr),
                "Cannot reach GitHub. Check your connection and try again.");
        DWORD status = 0, size = sizeof(status);
        require(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, nullptr),
                "Cannot read GitHub response.");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            require(asset && redirects < 5, "Unexpected update redirect.");
            url = utf8(header(request, WINHTTP_QUERY_LOCATION));
            continue;
        }
        require(status != 403 && status != 429,
                "GitHub temporarily limited update checks. Try again later.");
        require(status == 200,
                "GitHub returned HTTP " + std::to_string(status) + ". Try again later.");
        std::array<char, 65536> block;
        std::uint64_t total = 0;
        for (;;) {
            require(!cancelled, "Update cancelled.");
            DWORD count = 0;
            require(WinHttpReadData(request, block.data(), DWORD(block.size()), &count),
                    "The update download was interrupted. Try again.");
            if (!count)
                return;
            total += count;
            require(total <= limit, "Update response exceeds its expected size.");
            consume(block.data(), count);
        }
    }
    throw std::runtime_error("Too many download redirects.");
}
std::filesystem::path powershell() {
    std::array<wchar_t, MAX_PATH + 1> system{};
    require(GetSystemDirectoryW(system.data(), DWORD(system.size())),
            "Cannot find Windows PowerShell.");
    return std::filesystem::path(system.data()) / L"WindowsPowerShell/v1.0/powershell.exe";
}
std::vector<std::wstring> installer_arguments(const std::filesystem::path& root,
                                              const std::filesystem::path& job,
                                              const ReleaseUpdate& release) {
    return {L"-NoLogo",
            L"-NoProfile",
            L"-NonInteractive",
            L"-ExecutionPolicy",
            L"Bypass",
            L"-File",
            (job / L"apply-update.ps1").wstring(),
            L"-Root",
            root.wstring(),
            L"-Archive",
            (job / L"release.zip").wstring(),
            L"-ExpectedHash",
            wide(release.sha256),
            L"-ExpectedVersion",
            wide(release.version)};
}
} // namespace
std::string launcher_version() {
    return SEED_LAUNCHER_VERSION;
}
int compare_versions(const std::string& left, const std::string& right) {
    const auto a = version(left), b = version(right);
    return a < b ? -1 : a > b ? 1 : 0;
}
bool allowed_update_url(const std::string& url, bool redirect) {
    if (url.find_first_of("\r\n\\#") != std::string::npos)
        return false;
    if (url == kReleaseApi)
        return true;
    if (url.starts_with(std::string(repository) + "releases/download/"))
        return true;
    if (!redirect)
        return false;
    return url.starts_with("https://release-assets.githubusercontent.com/") ||
           url.starts_with("https://objects.githubusercontent.com/");
}
ReleaseUpdate parse_release_update(const std::string& text) {
    require(text.size() <= 2 * 1024 * 1024, "GitHub release response is too large.");
    Json json;
    require(gbarecomp::json::parse(text, json) && json.is_object(),
            "Invalid GitHub release response.");
    require(!json.flag("draft", true) && !json.flag("prerelease", true),
            "No stable GitHub release is available.");
    const auto tag = json.str("tag_name");
    require(std::regex_match(tag, std::regex(R"([A-Za-z0-9.-]{1,80})")), "Invalid release tag.");
    ReleaseUpdate result;
    result.name = json.str("name");
    result.notes = json.str("body").substr(0, 12000);
    result.page = json.str("html_url");
    require(result.page == std::string(repository) + "releases/tag/" + tag,
            "Unexpected release page.");
    const auto* assets = json.get("assets");
    require(assets && assets->is_array(), "GitHub release has no downloadable assets.");
    for (const auto& asset : assets->array) {
        const auto name = asset.str("name");
        if (!name.starts_with(asset_prefix) || !name.ends_with(".zip"))
            continue;
        require(result.asset.empty(), "Release has multiple supported portable archives.");
        result.version =
            name.substr(sizeof(asset_prefix) - 1, name.size() - (sizeof(asset_prefix) - 1) - 4);
        require(version(result.version).channel >= 0,
                "The portable archive is a development build.");
        require(compare_versions(result.version, tag) >= 0 &&
                    version(result.version).major == version(tag).major &&
                    version(result.version).minor == version(tag).minor &&
                    version(result.version).patch == version(tag).patch,
                "Release tag and portable version disagree.");
        result.asset = name;
        result.url = asset.str("browser_download_url");
        require(result.url == std::string(repository) + "releases/download/" + tag + "/" + name,
                "Portable download does not belong to this release.");
        result.sha256 = asset.str("digest");
        require(result.sha256.starts_with("sha256:"),
                "GitHub has not provided a verified download digest.");
        result.sha256.erase(0, 7);
        require(digest_valid(result.sha256), "Invalid GitHub download digest.");
        const auto size = asset.num("size", 0);
        require(std::isfinite(size) && size >= 1024 && size <= max_archive &&
                    std::floor(size) == size,
                "Invalid portable download size.");
        result.bytes = std::uint64_t(size);
    }
    require(!result.asset.empty(), "This release has no Windows x64 portable ZIP.");
    return result;
}
void download_update_archive(const ReleaseUpdate& release, const std::filesystem::path& target,
                             const std::atomic_bool& cancelled,
                             const std::function<void(float)>& progress) {
    require(allowed_update_url(release.url, false) && release.bytes >= 1024 &&
                release.bytes <= max_archive && digest_valid(release.sha256),
            "Invalid portable download contract.");
    require(!std::filesystem::exists(target), "Update download destination already exists.");
    try {
        std::ofstream file(target, std::ios::binary);
        require(bool(file), "Cannot write the update in the portable folder.");
        std::uint64_t received = 0;
        fetch(release.url, true, release.bytes, cancelled, [&](const char* bytes, unsigned size) {
            file.write(bytes, size);
            require(bool(file), "The update could not be saved completely.");
            received += size;
            if (progress)
                progress(float(double(received) / double(release.bytes)));
        });
        file.close();
        require(received == release.bytes && file_hash(target, false) == release.sha256,
                "Downloaded update failed SHA-256 verification. Check again and retry.");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(target, ignored);
        throw;
    }
}
bool application_in_use(const std::filesystem::path& root) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    require(snapshot != INVALID_HANDLE_VALUE, "Cannot check whether the game is running.");
    PROCESSENTRY32W entry{sizeof(entry)};
    bool busy = false;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (entry.th32ProcessID == GetCurrentProcessId())
                continue;
            if (_wcsicmp(entry.szExeFile, L"GundamSeedRecomp.exe") &&
                _wcsicmp(entry.szExeFile, L"GundamSeedLauncher.exe"))
                continue;
            HANDLE process =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            std::array<wchar_t, 32768> path{};
            DWORD length = DWORD(path.size());
            if (QueryFullProcessImageNameW(process, 0, path.data(), &length))
                busy |= _wcsicmp(std::filesystem::path(path.data()).parent_path().c_str(),
                                 root.c_str()) == 0;
            CloseHandle(process);
        } while (!busy && Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return busy;
}
struct LauncherUpdates::Impl {
    std::filesystem::path root, job;
    mutable std::mutex mutex;
    UpdateState current;
    std::thread worker;
    std::atomic_bool cancelled = false;
    GameProcess installer;
    explicit Impl(std::filesystem::path directory) : root(std::move(directory)) {
        const auto report = root / L".updates/last-result.json";
        if (std::filesystem::is_regular_file(report) &&
            std::filesystem::file_size(report) < 65536) {
            std::ifstream file(report);
            std::string text((std::istreambuf_iterator<char>(file)), {});
            Json value;
            if (gbarecomp::json::parse(text, value))
                current.message = value.str("message");
        }
    }
    UpdateState state() const {
        std::lock_guard lock(mutex);
        return current;
    }
    void message(UpdatePhase phase, std::string text) {
        std::lock_guard lock(mutex);
        current.phase = phase;
        current.message = std::move(text);
    }
    bool begin(UpdatePhase phase) {
        const auto previous = state().phase;
        if (previous == UpdatePhase::Checking || previous == UpdatePhase::Downloading)
            return false;
        if (worker.joinable())
            worker.join();
        cancelled = false;
        {
            std::lock_guard lock(mutex);
            current.progress = 0;
        }
        message(phase, phase == UpdatePhase::Checking ? "Checking GitHub releases..."
                                                      : "Downloading update...");
        return true;
    }
    void fail(const std::exception& error) {
        message(UpdatePhase::Failed, error.what());
    }
};
LauncherUpdates::LauncherUpdates(std::filesystem::path root)
    : impl_(std::make_unique<Impl>(std::move(root))) {}
LauncherUpdates::~LauncherUpdates() {
    impl_->cancelled = true;
    if (impl_->worker.joinable())
        impl_->worker.join();
}
UpdateState LauncherUpdates::state() const {
    return impl_->state();
}
void LauncherUpdates::check() {
    auto& task = *impl_;
    if (!task.begin(UpdatePhase::Checking))
        return;
    task.worker = std::thread([&task] {
        try {
            std::string json;
            fetch(kReleaseApi, false, 2 * 1024 * 1024, task.cancelled,
                  [&](const char* bytes, unsigned size) { json.append(bytes, size); });
            auto release = parse_release_update(json);
            const bool newer = compare_versions(release.version, launcher_version()) > 0;
            const bool same = compare_versions(release.version, launcher_version()) == 0;
            std::lock_guard lock(task.mutex);
            task.current.release = std::move(release);
            task.current.phase = newer ? UpdatePhase::Available : UpdatePhase::Current;
            task.current.message = newer  ? "A newer version is available."
                                   : same ? "You are up to date."
                                          : "This build is newer than the latest public release.";
        } catch (const std::exception& error) {
            task.fail(error);
        }
    });
}
void LauncherUpdates::download() {
    auto& task = *impl_;
    const auto release = task.state().release;
    if (release.asset.empty() || compare_versions(release.version, launcher_version()) <= 0 ||
        !task.begin(UpdatePhase::Downloading))
        return;
    task.worker = std::thread([&task, release] {
        try {
            GUID guid{};
            require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create update staging folder.");
            wchar_t name[40]{};
            StringFromGUID2(guid, name, 40);
            const auto staging = task.root / L".updates";
            std::filesystem::create_directories(staging);
            require(!(GetFileAttributesW(staging.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
                    "Update staging cannot be a link or junction.");
            task.job = staging / name;
            require(std::filesystem::create_directory(task.job), "Update staging already exists.");
            std::filesystem::copy_file(task.root / L"Update-Portable.ps1",
                                       task.job / L"apply-update.ps1");
            const auto archive = task.job / L"release.zip";
            download_update_archive(release, archive, task.cancelled, [&](float progress) {
                std::lock_guard lock(task.mutex);
                task.current.progress = progress;
            });
            task.message(UpdatePhase::Downloading, "Verifying the portable files...");
            auto args = installer_arguments(task.root, task.job, release);
            args.push_back(L"-ValidateOnly");
            GameProcess validation;
            std::string error;
            require(validation.start(powershell(), args, task.job / L"validation.log", error),
                    error);
            std::optional<unsigned long> code;
            while (!(code = validation.poll()))
                Sleep(40);
            require(*code == 0, "The package could not be verified. See .updates validation.log.");
            task.message(UpdatePhase::Ready,
                         "Update verified. Install and restart when you are ready.");
        } catch (const std::exception& error) {
            task.fail(error);
        }
    });
}
bool LauncherUpdates::install(std::string& error) {
    try {
        auto& task = *impl_;
        const auto value = task.state();
        require(value.phase == UpdatePhase::Ready, "Download and verify an update first.");
        if (task.worker.joinable())
            task.worker.join();
        require(!application_in_use(task.root),
                "Close the game and other launchers in this folder before installing.");
        auto args = installer_arguments(task.root, task.job, value.release);
        args.insert(args.end(), {L"-ParentId", std::to_wstring(GetCurrentProcessId())});
        return task.installer.start(powershell(), args, task.job / L"install.log", error);
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}
} // namespace seed::launcher
