#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "platform.h"
#include "settings.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace sl = seed::launcher;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--echo") {
        std::cout << sl::utf8(argv[2]);
        return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--fail")
        return 3;
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: seed_launcher_tests SCRATCH_DIRECTORY");
        const std::filesystem::path scratch = argv[1];
        std::filesystem::create_directories(scratch);
        const auto data = scratch / L"hash sample.bin";
        {
            std::ofstream file(data, std::ios::binary);
            file << "abc";
        }
        require(sl::file_hash(data, false) ==
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "SHA-256 mismatch");
        require(sl::file_hash(data, true) == "a9993e364706816aba3e25717850c26c9cd0d89d",
                "SHA-1 mismatch");
        sl::GameFile file{sl::FileKind::US};
        file.path = sl::utf8(data.wstring());
        file.verify();
        require(!file.verified, "Wrong-size ROM accepted");
        file.path = sl::utf8((scratch / L"missing.gba").wstring());
        file.verify();
        require(!file.verified, "Missing ROM accepted");
        file.verified = true;
        file.edited();
        require(!file.verified, "Editing did not invalidate identity");
        sl::Settings defaults;
        for (unsigned index = 0; index < defaults.files.size(); ++index)
            require(defaults.files[index].path == sl::kPortableGamePaths[index],
                    "Default gamefiles path is missing");
        defaults.load(scratch / L"missing-settings.ini");
        for (const auto& selected : defaults.files) {
            require(std::filesystem::path(sl::wide(selected.path)).parent_path() ==
                        std::filesystem::absolute(scratch) / L"gamefiles",
                    "Fresh launcher paths did not resolve inside its portable gamefiles folder");
            require(!selected.verified, "Missing default input was accepted");
        }
        const auto settings_path = scratch / L"launcher-settings.ini";
        sl::Settings original;
        original.scale = 5;
        original.files[0].path = "D:\\ROM folder\\US's \\\"path.gba";
        original.files[1].path = sl::utf8(L"D:\\日本語\\Japanese.gba");
        original.files[2].path = "D:\\BIOS folder\\gba.bin";
        std::string error;
        require(original.save(settings_path, error), "Settings save failed");
        original.scale = 4;
        require(original.save(settings_path, error), "Atomic settings replacement failed");
        sl::Settings loaded;
        loaded.load(settings_path);
        require(loaded.scale == 4, "Scale did not persist");
        for (unsigned index = 0; index < 3; ++index)
            require(loaded.files[index].path == original.files[index].path,
                    "Path round-trip failed");
        const auto portable = std::filesystem::absolute(scratch / L"portable-original");
        std::filesystem::create_directories(portable / L"gamefiles");
        {
            std::ofstream sample(portable / L"gamefiles" / L"us.gba", std::ios::binary);
            sample << "abc";
        }
        sl::Settings portable_settings;
        portable_settings.files[0].path = sl::utf8((portable / L"gamefiles" / L"us.gba").wstring());
        portable_settings.files[1].path = original.files[1].path;
        require(portable_settings.save(portable / L"launcher-settings.ini", error),
                "Portable settings save failed");
        {
            std::ifstream saved(portable / L"launcher-settings.ini");
            std::ostringstream contents;
            contents << saved.rdbuf();
            require(contents.str().find("gamefiles/us.gba") != std::string::npos,
                    "Bundled path was not stored relative to the launcher");
            require(contents.str().find(sl::utf8(portable.wstring())) == std::string::npos,
                    "Portable settings retained their original absolute directory");
        }
        const auto relocated = std::filesystem::absolute(scratch / L"portable relocated 日本語");
        std::filesystem::rename(portable, relocated);
        sl::Settings relocated_settings;
        relocated_settings.load(relocated / L"launcher-settings.ini");
        require(std::filesystem::path(sl::wide(relocated_settings.files[0].path)) ==
                    relocated / L"gamefiles" / L"us.gba",
                "Moved settings resolved relative to the working directory");
        require(relocated_settings.files[0].message == "This file has the wrong cartridge size",
                "Moved bundled file was not found from an unrelated working directory");
        require(relocated_settings.files[1].path == original.files[1].path,
                "External cartridge selection changed during portable relocation");
        require(relocated_settings.save(relocated / L"launcher-settings.ini", error),
                "Moved portable settings could not be saved");
        for (const auto& value :
             {std::wstring(), std::wstring(L"plain"), std::wstring(L"C:\\with spaces\\"),
              std::wstring(L"quotes \\\" here"), std::wstring(L"日本語's path")}) {
            std::wstring command = L"program.exe " + sl::quote_argument(value);
            int count = 0;
            LPWSTR* parsed = CommandLineToArgvW(command.c_str(), &count);
            require(parsed && count == 2 && value == parsed[1], "Windows argument quoting failed");
            LocalFree(parsed);
        }
        const auto log = scratch / L"child process.log";
        sl::GameProcess process;
        require(!process.start(scratch / L"missing.exe", {}, log, error),
                "Missing game executable accepted");
        const auto self = sl::executable_directory() / L"seed_launcher_tests.exe";
        const std::wstring payload = L"A path with spaces, 日本語 and trailing\\";
        require(process.start(self, {L"--echo", payload}, log, error), "Child start failed");
        std::optional<unsigned long> code;
        for (int attempt = 0; attempt < 200 && !code; ++attempt) {
            code = process.poll();
            if (!code)
                Sleep(10);
        }
        require(code && *code == 0, "Child did not exit successfully");
        std::ifstream output(log, std::ios::binary);
        std::ostringstream text;
        text << output.rdbuf();
        output.close();
        require(text.str() == sl::utf8(payload), "Child arguments were changed");
        require(process.start(self, {L"--fail"}, log, error), "Second child start failed");
        code.reset();
        for (int attempt = 0; attempt < 200 && !code; ++attempt) {
            code = process.poll();
            if (!code)
                Sleep(10);
        }
        require(code && *code == 3, "Failure exit status was lost");
        std::cout
            << "PASS: file hashing, invalid-file rejection, settings persistence, portable "
               "relocation, Unicode paths, Windows quoting, child launch and exit reporting.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
