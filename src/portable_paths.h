#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <stdexcept>
namespace seed {
inline std::filesystem::path settings_root(const std::filesystem::path& file) {
    const auto parent = file.parent_path();
    return parent.filename() == "config" ? parent.parent_path() : parent;
}
inline std::filesystem::path config_file(const std::filesystem::path& root,
                                         const std::filesystem::path& name) {
    return root / "config" / name;
}
inline std::filesystem::path resource_file(const std::filesystem::path& root,
                                           const std::filesystem::path& name) {
    return root / "resource" / name;
}
inline std::filesystem::path log_file(const std::filesystem::path& root,
                                      const std::filesystem::path& name) {
    return root / "Logs" / name;
}
inline std::string file_text(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read portable settings file: " + file.filename().string());
    std::string text{std::istreambuf_iterator<char>(stream), {}};
    if (stream.bad())
        throw std::runtime_error("Portable settings file could not be read completely.");
    return text;
}
// A fresh portable's defaults must not conceal a legacy player's settings
// when they extract an update over an existing folder.
inline bool default_launcher_settings(const std::filesystem::path& file) {
    auto text = file_text(file);
    for (std::size_t i = 0; (i = text.find('\r', i)) != std::string::npos;)
        text.erase(i, 1);
    return text == "us \"gamefiles/Mobile Suit Gundam Seed - Battle Assault (USA).gba\"\n"
                   "japanese \"gamefiles/Kidou Senshi Gundam - Seed Destiny (Japan).gba\"\n"
                   "bios \"gamefiles/gba_bios.bin\"\nscale \"3\"\n";
}
inline void require_plain_folder(const std::filesystem::path& path) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        throw std::runtime_error("Portable settings folders cannot be symbolic links.");
    std::filesystem::create_directories(path);
}
inline void prepare_portable_folders(const std::filesystem::path& root) {
    for (const auto name : {"config", "resource", "Logs"})
        require_plain_folder(root / name);
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_symlink() || !entry.is_regular_file())
            continue;
        const auto name = entry.path().filename();
        const auto extension = name.extension().string();
        std::filesystem::path destination;
        if (extension == ".cfg" || extension == ".frag" || extension == ".ini" ||
            extension == ".ps1" || extension == ".json" || name == "player-name.txt")
            destination = config_file(root, name);
        else if (extension == ".log")
            destination = log_file(root, name);
        else if (extension == ".ico" || name == "controllerdb.txt")
            destination = resource_file(root, name);
        if (destination.empty())
            continue;
        try {
            if (std::filesystem::exists(destination)) {
                if (file_text(entry.path()) == file_text(destination)) {
                    std::filesystem::remove(entry.path());
                    continue;
                }
                const bool prefer_legacy =
                    name == "launcher-settings.ini" && default_launcher_settings(destination);
                const auto backup_folder = destination.parent_path() / "migration-backups";
                require_plain_folder(backup_folder);
                auto backup = backup_folder / name;
                for (unsigned n = 1; std::filesystem::exists(backup); ++n)
                    backup = backup_folder / (name.string() + "." + std::to_string(n));
                // Preserve both versions on conflict; never overwrite personal data.
                std::filesystem::rename(prefer_legacy ? destination : entry.path(), backup);
                if (!prefer_legacy)
                    continue;
            }
            std::filesystem::rename(entry.path(), destination);
        } catch (const std::exception&) {
            // A console redirect or an older running instance may hold a log.
            // Leave it intact for the next launch; settings errors stay fatal.
            if (extension != ".log")
                throw;
        }
    }
    const auto old_updates = root / ".updates", new_updates = root / "config/updates";
    if (std::filesystem::is_directory(old_updates) && !std::filesystem::is_symlink(old_updates) &&
        !std::filesystem::exists(new_updates))
        std::filesystem::rename(old_updates, new_updates);
    const auto licenses = root / "licenses", relocated = root / "resource/licenses";
    if (std::filesystem::is_directory(licenses) && !std::filesystem::is_symlink(licenses)) {
        require_plain_folder(relocated);
        for (const auto& entry : std::filesystem::directory_iterator(licenses)) {
            if (entry.is_symlink() || !entry.is_regular_file())
                continue;
            const auto destination = relocated / entry.path().filename();
            if (!std::filesystem::exists(destination))
                std::filesystem::rename(entry.path(), destination);
            else if (file_text(entry.path()) == file_text(destination))
                std::filesystem::remove(entry.path());
            else {
                const auto backups = relocated / "previous";
                require_plain_folder(backups);
                auto target = backups / entry.path().filename();
                for (unsigned n = 1; std::filesystem::exists(target); ++n)
                    target = backups / (entry.path().filename().string() + "." + std::to_string(n));
                std::filesystem::rename(entry.path(), target);
            }
        }
        if (std::filesystem::is_empty(licenses))
            std::filesystem::remove(licenses);
    }
}
} // namespace seed
