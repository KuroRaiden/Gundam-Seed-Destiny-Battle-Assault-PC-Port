#include "jp_combat_runtime.h"
#include <vector>
#include <string>
int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    seed::JpCombatRequest request;
    request.fighters[0] = 13;
    request.fighters[1] = 14;
    std::vector<char*> args{argv[0]};
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--fighters" && i + 2 < argc) {
            request.fighters[0] = std::stoi(argv[++i]);
            request.fighters[1] = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "--training")
            request.mode = seed::JpCombatMode::Training;
        else
            args.push_back(argv[i]);
    }
    seed::configure_jp_combat(options, request);
    return gbarecomp::run_game(int(args.size()), args.data(), options);
}
