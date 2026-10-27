#include "update.h"
#include "platform.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <thread>
using namespace seed::launcher;
void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
void rejects(const std::function<void()>& action, const char* text) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, text);
}
std::string metadata() {
    return R"({"draft":false,"prerelease":false,"tag_name":"v0.1.3","name":"0.1.3 fixed",
"html_url":"https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/tag/v0.1.3",
"body":"Fixed release notes","assets":[{"name":"GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-0.1.3-fixed.zip",
"browser_download_url":"https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/download/v0.1.3/GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-0.1.3-fixed.zip",
"digest":"sha256:5f2d4e6a1f2ea06b862ee21709741033672ddb84f82bdc828fc8fc6bc5d46a38","size":30941840}]})";
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--hold") {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            return 0;
        }
        require(launcher_release_name().starts_with(launcher_version()),
                "Release label does not match version");
        require(compare_versions("0.1.10", "v0.1.9") > 0, "Version numbers sorted lexically");
        require(compare_versions("0.1.3-fixed", "0.1.3") > 0,
                "Fixed release did not supersede original");
        require(compare_versions("0.1.4-dev", "0.1.3-fixed") > 0,
                "Development build would downgrade");
        require(compare_versions("0.1.4", "0.1.4-dev") > 0,
                "Stable build did not supersede development");
        require(compare_versions("0.1.3-fixed2", "0.1.3-fixed") > 0,
                "Fixed revisions did not sort");
        require(compare_versions("v0.1.3", "0.1.3") == 0, "Tag prefix changed version");
        for (auto bad : {"", "latest", "0.1.3-foo", "0.1.3/evil", "0.1.3\n", "999999.1.1"})
            rejects([&] { compare_versions(bad, "0.1.3"); }, "Invalid version accepted");
        const auto valid = metadata();
        const auto release = parse_release_update(valid);
        require(release.version == "0.1.3-fixed" && release.bytes == 30941840 &&
                    release.sha256.size() == 64,
                "Fixed release assets not recognized");
        auto changed = [&](const std::string& from, const std::string& to) {
            auto value = valid;
            const auto at = value.find(from);
            require(at != std::string::npos, "Malformed test fixture");
            value.replace(at, from.size(), to);
            return value;
        };
        for (auto value :
             {std::string("{}"), std::string("invalid JSON"),
              changed("\"draft\":false", "\"draft\":true"),
              changed("\"prerelease\":false", "\"prerelease\":true"),
              changed("sha256:5f2d", "sha256:zzzz"), changed("30941840", "9999999999"),
              changed("30941840", "-1"), changed("/download/v0.1.3/", "/download/v0.1.4/"),
              changed("\"tag_name\":\"v0.1.3\"", "\"tag_name\":\"v0.1.2\"")})
            rejects([&] { parse_release_update(value); }, "Untrusted/invalid release was accepted");
        require(allowed_update_url(kReleaseApi, false), "Official API rejected");
        require(allowed_update_url(release.url, false), "Official asset rejected");
        require(allowed_update_url("https://release-assets.githubusercontent.com/asset?signature=x",
                                   true),
                "GitHub signed redirect rejected");
        for (auto bad : {"http://api.github.com/", "https://github.com.evil.example/asset",
                         "https://release-assets.githubusercontent.com.evil.example/asset",
                         "https://user:password@release-assets.githubusercontent.com/asset",
                         "https://raw.githubusercontent.com/someone/file",
                         "https://release-assets.githubusercontent.com/asset#fragment"})
            require(!allowed_update_url(bad, true), "Unexpected update host accepted");
        if (argc == 3 &&
            (std::string(argv[1]) == "--live-check" || std::string(argv[1]) == "--live-download")) {
            LauncherUpdates updates{std::filesystem::path(argv[2])};
            updates.check();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            while (updates.state().phase == UpdatePhase::Checking &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const auto process_root = std::filesystem::path(argv[2]) / "process-check";
            std::filesystem::create_directories(process_root);
            const auto sentinel = process_root / "GundamSeedRecomp.exe";
            std::filesystem::copy_file(std::filesystem::absolute(argv[0]), sentinel);
            require(!application_in_use(process_root), "Idle fixture reported a running game");
            GameProcess child;
            std::string process_error;
            require(
                child.start(sentinel, {L"--hold"}, process_root / "sentinel.log", process_error),
                process_error.c_str());
            require(application_in_use(process_root),
                    "Running game in this portable was not detected");
            while (!child.poll())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            require(!application_in_use(process_root), "Exited game still blocks installation");
            std::puts("PASS: running-game update guard and release after game exit");
            const auto state = updates.state();
            require(state.phase == UpdatePhase::Current || state.phase == UpdatePhase::Available,
                    state.message.c_str());
            if (std::string(argv[1]) == "--live-download") {
                std::atomic_bool cancelled = false;
                download_update_archive(state.release,
                                        std::filesystem::path(argv[2]) / "live-download.zip",
                                        cancelled, {});
                std::puts(
                    "PASS: native HTTPS asset download, GitHub redirect and SHA-256 verification");
            }
            std::printf("PASS: live GitHub check: installed %s, latest %s, %s\n",
                        launcher_version().c_str(), state.release.version.c_str(),
                        state.message.c_str());
        }
        std::puts("PASS: semantic versions, fixed releases, downgrade prevention, release "
                  "validation and HTTPS host restrictions");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
