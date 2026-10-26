#pragma once
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

namespace seed {
struct ControllerDbResult {
    bool loaded = false;
    int added = 0;
    std::string message;
};
// Call after SDL_INIT_GAMEPAD and before opening controllers. Resolve beside
// the executable, so shortcuts and unrelated working directories behave alike.
inline ControllerDbResult load_controller_db(const std::filesystem::path& folder) {
    const auto file = folder / "controllerdb.txt";
    const auto utf8 = file.u8string();
    const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        std::fprintf(stderr, "controller_db: %s missing; using built-in SDL3 mappings\n",
                     path.c_str());
        return {false, 0, "controllerdb.txt missing; using built-in SDL3 mappings."};
    }
    const int added = SDL_AddGamepadMappingsFromFile(path.c_str());
    if (added < 0) {
        std::string message = std::string("controllerdb.txt could not load: ") + SDL_GetError();
        std::fprintf(stderr, "controller_db: %s\n", message.c_str());
        return {false, 0, std::move(message)};
    }
    std::fprintf(stderr, "controller_db: loaded %s (%d mappings added)\n", path.c_str(), added);
    return {true, added, "controllerdb.txt loaded."};
}
inline ControllerDbResult load_controller_db() {
    const auto* base = SDL_GetBasePath();
    if (!base)
        return {false, 0, "Could not resolve the controller database folder."};
    return load_controller_db(std::filesystem::u8path(base));
}
} // namespace seed
