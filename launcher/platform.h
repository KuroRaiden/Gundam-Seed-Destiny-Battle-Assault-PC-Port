#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seed::launcher {
std::filesystem::path executable_directory();
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
std::wstring quote_argument(const std::wstring& value);
std::optional<std::string> pick_file(void* owner, bool bios,
                                     const std::filesystem::path& initial_folder = {});
std::string file_hash(const std::filesystem::path& path, bool sha1);
void open_file(const std::filesystem::path& path);
bool replace_settings_file(const std::filesystem::path& staging, const std::filesystem::path& path,
                           std::string& error);

class GameProcess {
  public:
    ~GameProcess();
    bool start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
               const std::filesystem::path& log, std::string& error);
    std::optional<unsigned long> poll();
    bool running() const {
        return process_ != nullptr;
    }

  private:
    void* process_ = nullptr;
};
} // namespace seed::launcher
