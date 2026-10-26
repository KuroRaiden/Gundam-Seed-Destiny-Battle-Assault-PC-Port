#include "jp_combat_runtime.h"
#include "jp_online.h"
#include <filesystem>
#include <string>
#include <vector>
#include <exception>
#include <cstdio>
extern "C" __declspec(dllexport) int SeedRunJpCombat(const seed::JpCombatRequest* request) {
    if (!request || request->version != seed::kCombatApiVersion || !request->rom ||
        !request->bios || !request->save)
        return -1;
    for (const auto id : request->fighters)
        if (id < 1 || (id > 23 && id != 25))
            return -1;
    if (request->mode != seed::JpCombatMode::Versus &&
        request->mode != seed::JpCombatMode::Training)
        return -1;
    try {
        gbarecomp::RunOptions options;
        seed::configure_jp_combat(options, *request);
        if (request->surface.online)
            seed::configure_jp_online(options, *request);
        std::filesystem::path online_save;
        if (request->surface.online) {
            online_save = std::filesystem::u8path(request->save);
            online_save += ".online-" + std::to_string(request->connection.epoch) + "-" +
                           std::to_string(request->connection.seat);
            // Do not inherit a player's unrelated EEPROM into the initial
            // rollback state. This private match file is removed on return.
            if (std::filesystem::exists(online_save))
                return -3;
        }
        struct TemporarySave {
            std::filesystem::path path;
            ~TemporarySave() {
                if (!path.empty()) {
                    std::error_code error;
                    std::filesystem::remove(path, error);
                }
            }
        } temporary{online_save};
        const auto save = online_save.empty() ? std::string(request->save) : online_save.string();
        std::vector<std::string> args{"GundamSeedRecomp.exe", "--rom",      request->rom, "--bios",
                                      request->bios,          "--bios-hle", "--save",     save};
        if (request->surface.window)
            args.push_back("--window");
        if (request->debug_port > 0) {
            args.push_back(request->surface.window ? "--tcp-observe" : "--tcp");
            args.push_back(std::to_string(request->debug_port));
        }
        if (!request->surface.window && !request->debug_port && !request->qualification_frames &&
            !request->qualification_end_frame)
            return -2;
        std::vector<char*> argv;
        for (auto& arg : args)
            argv.push_back(arg.data());
        const int result = gbarecomp::run_game(int(argv.size()), argv.data(), options);
        const bool closed = seed::combat_window_closed();
        seed::borrow_combat_surface({});
        return result ? -result : closed ? 2 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "jp_combat: %s\n", e.what());
        return -1;
    }
}
