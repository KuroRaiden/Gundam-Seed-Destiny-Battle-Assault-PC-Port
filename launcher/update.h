#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>

namespace seed::launcher {
inline constexpr char kReleasePage[] =
    "https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases";
inline constexpr char kReleaseApi[] = "https://api.github.com/repos/KuroRaiden/"
                                      "Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/latest";
struct ReleaseUpdate {
    std::string version, name, notes, page, asset, url, sha256;
    std::uint64_t bytes = 0;
};
// Reject malformed versions; fixed releases sort after their original version.
int compare_versions(const std::string& left, const std::string& right);
ReleaseUpdate parse_release_update(const std::string& json);
bool allowed_update_url(const std::string& url, bool redirect);
std::string launcher_version();
std::string launcher_release_name();
bool application_in_use(const std::filesystem::path& root);
void download_update_archive(const ReleaseUpdate& release, const std::filesystem::path& target,
                             const std::atomic_bool& cancelled,
                             const std::function<void(float)>& progress);

enum class UpdatePhase { Idle, Checking, Current, Available, Downloading, Ready, Failed };
struct UpdateState {
    UpdatePhase phase = UpdatePhase::Idle;
    ReleaseUpdate release;
    std::string message;
    float progress = 0;
};
class LauncherUpdates {
  public:
    explicit LauncherUpdates(std::filesystem::path root);
    ~LauncherUpdates();
    UpdateState state() const;
    void check();
    void download();
    // Starts a detached installer that waits for this launcher to exit.
    // Caller saves local preferences and exits only when this returns true.
    bool install(std::string& error);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace seed::launcher
