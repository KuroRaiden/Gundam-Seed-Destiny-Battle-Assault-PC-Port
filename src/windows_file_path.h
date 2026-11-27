#pragma once
#include <filesystem>
namespace seed {
inline std::filesystem::path windows_file_path(const std::filesystem::path& path) {
    auto name = std::filesystem::absolute(path).lexically_normal().make_preferred().wstring();
    if (name.starts_with(L"\\\\?\\"))
        return name;
    return name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
}
} // namespace seed
