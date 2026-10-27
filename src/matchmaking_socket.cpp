#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include "matchmaking_socket.h"
#include "mini_json.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
namespace seed {
namespace {
using Clock = std::chrono::steady_clock;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Handle {
    HINTERNET value = nullptr;
    ~Handle() {
        if (value)
            WinHttpCloseHandle(value);
    }
};
struct Endpoint {
    std::wstring host, path;
    INTERNET_PORT port;
    bool secure;
};
Endpoint endpoint(const std::string& url) {
    // Production is pinned to our HTTPS service. Only exact loopback HTTP is
    // permitted for explicit test fixtures; no redirects or embedded secrets.
    require(url == kMatchmakingUrl || url.starts_with("http://127.0.0.1:"),
            "Unexpected matchmaking service address.");
    std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    require(WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &parts),
            "Invalid service address.");
    Endpoint result{std::wstring(parts.lpszHostName, parts.dwHostNameLength),
                    std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength), parts.nPort,
                    parts.nScheme == INTERNET_SCHEME_HTTPS};
    require(!parts.dwUserNameLength && !parts.dwPasswordLength && !parts.dwExtraInfoLength &&
                result.path == L"/v1/connect" && (result.secure || result.host == L"127.0.0.1"),
            "Invalid service address.");
    return result;
}
} // namespace
struct MatchmakingSocket::Job {
    struct Notice {
        DWORD kind = 0, bytes = 0, error = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
    };
    std::atomic_bool stopping{false}, finished{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Notice> notices;
    std::deque<std::string> outgoing, incoming;
    unsigned tracked_handles = 0;
    std::thread worker;
    explicit Job(Endpoint address)
        : worker([this, address = std::move(address)] { run(address); }) {}
    ~Job() {
        stop();
        if (worker.joinable())
            worker.join();
    }
    void stop() {
        stopping = true;
        changed.notify_all();
    }
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD kind, void* info,
                                  DWORD size) {
        if (!context)
            return;
        auto& self = *reinterpret_cast<Job*>(context);
        std::lock_guard lock(self.mutex);
        if (kind == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            if (self.tracked_handles)
                --self.tracked_handles;
        } else if (kind == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ||
                   kind == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE ||
                   kind == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                   kind == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                   kind == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            Notice notice{kind};
            if (kind == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR &&
                size >= sizeof(WINHTTP_ASYNC_RESULT))
                notice.error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
            if (kind == WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
                size >= sizeof(WINHTTP_WEB_SOCKET_STATUS)) {
                const auto& status = *static_cast<WINHTTP_WEB_SOCKET_STATUS*>(info);
                notice.bytes = status.dwBytesTransferred;
                notice.type = status.eBufferType;
            }
            self.notices.push_back(notice);
        }
        self.changed.notify_all();
    }
    void track(HINTERNET handle) {
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        require(WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)),
                "Cannot track service connection.");
        std::lock_guard lock(mutex);
        ++tracked_handles;
    }
    void publish(std::string text) {
        std::lock_guard lock(mutex);
        require(incoming.size() < 128, "Too many service messages.");
        incoming.push_back(std::move(text));
    }
    Notice wait() {
        std::unique_lock lock(mutex);
        changed.wait_for(lock, std::chrono::milliseconds(100),
                         [&] { return stopping || !notices.empty(); });
        require(!stopping, "Matchmaking cancelled.");
        if (notices.empty())
            return {};
        auto notice = notices.front();
        notices.pop_front();
        require(notice.kind != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR,
                "Matchmaking connection interrupted. Try again.");
        return notice;
    }
    void until(DWORD wanted) {
        const auto deadline = Clock::now() + std::chrono::seconds(12);
        for (;;) {
            require(Clock::now() < deadline, "Matchmaking service timed out. Try again.");
            if (wait().kind == wanted)
                return;
        }
    }
    void run(const Endpoint& address) {
        // This scope closes child handles while callback storage is still alive.
        // HANDLE_CLOSING is the final notification; drain it before job teardown.
        {
            Handle session, connection, request, socket;
            try {
                session.value =
                    WinHttpOpen(L"GundamSeedPC-Matchmaking/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
                require(session.value, "Windows could not initialize matchmaking.");
                require(WinHttpSetStatusCallback(session.value, callback,
                                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                                     WINHTTP_CALLBACK_FLAG_HANDLES,
                                                 0) != WINHTTP_INVALID_STATUS_CALLBACK,
                        "Cannot initialize service callbacks.");
                WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);
                connection.value =
                    WinHttpConnect(session.value, address.host.c_str(), address.port, 0);
                require(connection.value, "Cannot connect to matchmaking.");
                request.value = WinHttpOpenRequest(
                    connection.value, L"GET", address.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES, address.secure ? WINHTTP_FLAG_SECURE : 0);
                require(request.value, "Cannot create matchmaking request.");
                track(request.value);
                DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
                require(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                                         sizeof(redirect)),
                        "Cannot restrict service redirects.");
                DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
                WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon,
                                 sizeof(autologon));
                require(WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                                         nullptr, 0),
                        "Windows WebSocket support is unavailable.");
                require(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr,
                                           0, 0, reinterpret_cast<DWORD_PTR>(this)),
                        "Cannot reach matchmaking. Check your connection.");
                until(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
                require(WinHttpReceiveResponse(request.value, nullptr),
                        "Cannot read service response.");
                until(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
                DWORD status = 0, size = sizeof(status);
                require(WinHttpQueryHeaders(request.value,
                                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, nullptr),
                        "Cannot read service status.");
                require(status != 429, "Matchmaking is full. Try again later.");
                require(status == 101,
                        "Matchmaking service unavailable. Host and Join still work.");
                socket.value = WinHttpWebSocketCompleteUpgrade(request.value,
                                                               reinterpret_cast<DWORD_PTR>(this));
                require(socket.value, "Cannot upgrade the matchmaking connection.");
                {
                    std::lock_guard lock(mutex);
                    ++tracked_handles;
                }
                publish("{\"type\":\"socket_open\"}");
                std::array<char, 4096> buffer{};
                std::string message, sending;
                bool receiving = false, writing = false;
                auto heartbeat = Clock::now();
                while (!stopping) {
                    if (!receiving) {
                        require(WinHttpWebSocketReceive(socket.value, buffer.data(),
                                                        DWORD(buffer.size()), nullptr,
                                                        nullptr) == NO_ERROR,
                                "Cannot receive matchmaking messages.");
                        receiving = true;
                    }
                    if (!writing) {
                        {
                            std::lock_guard lock(mutex);
                            if (!outgoing.empty()) {
                                sending = std::move(outgoing.front());
                                outgoing.pop_front();
                            }
                        }
                        if (sending.empty() &&
                            Clock::now() - heartbeat >= std::chrono::seconds(20)) {
                            sending = "ping";
                            heartbeat = Clock::now();
                        }
                        if (!sending.empty()) {
                            require(WinHttpWebSocketSend(
                                        socket.value, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                        sending.data(), DWORD(sending.size())) == NO_ERROR,
                                    "Cannot send matchmaking message.");
                            writing = true;
                        }
                    }
                    const auto notice = wait();
                    if (notice.kind == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE) {
                        writing = false;
                        sending.clear();
                    }
                    if (notice.kind != WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
                        continue;
                    receiving = false;
                    require(notice.type != WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE,
                            "Matchmaking disconnected. Try again.");
                    require(notice.type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
                                notice.type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                            "Invalid service message.");
                    require(notice.bytes <= buffer.size() && message.size() + notice.bytes <= 4096,
                            "Service message is too large.");
                    message.append(buffer.data(), notice.bytes);
                    if (notice.type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                        if (message != "pong")
                            publish(std::move(message));
                        message.clear();
                    }
                }
            } catch (const std::exception& error) {
                if (!stopping) {
                    std::string value = "{\"type\":\"socket_error\",\"message\":";
                    gbarecomp::json::append_quoted(value, error.what());
                    value += '}';
                    // The queue may already be full; replace it with the terminal error.
                    std::lock_guard lock(mutex);
                    incoming.clear();
                    incoming.push_back(std::move(value));
                }
            }
        }
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return tracked_handles == 0; });
        }
        finished = true;
    }
};
MatchmakingSocket::MatchmakingSocket() = default;
MatchmakingSocket::~MatchmakingSocket() = default;
void MatchmakingSocket::collect() {
    std::erase_if(retired_, [](const auto& job) { return job->finished.load(); });
}
void MatchmakingSocket::disconnect() {
    if (job_) {
        job_->stop();
        retired_.push_back(std::move(job_));
    }
    collect();
}
void MatchmakingSocket::connect(const std::string& url) {
    const auto address = endpoint(url);
    disconnect();
    require(retired_.size() < 4, "Please wait for the previous connection to close.");
    job_ = std::make_unique<Job>(address);
}
void MatchmakingSocket::send(std::string text) {
    if (!job_ || job_->stopping || job_->finished)
        return;
    require(text.size() <= 4096, "Matchmaking request too large.");
    std::lock_guard lock(job_->mutex);
    require(job_->outgoing.size() < 64, "Too many matchmaking requests.");
    job_->outgoing.push_back(std::move(text));
    job_->changed.notify_all();
}
std::vector<std::string> MatchmakingSocket::take() {
    collect();
    std::vector<std::string> result;
    if (job_) {
        std::lock_guard lock(job_->mutex);
        while (!job_->incoming.empty()) {
            result.push_back(std::move(job_->incoming.front()));
            job_->incoming.pop_front();
        }
    }
    return result;
}
bool MatchmakingSocket::active() const {
    return job_ && !job_->stopping && !job_->finished;
}
} // namespace seed
