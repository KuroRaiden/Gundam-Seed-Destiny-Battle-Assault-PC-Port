#include "runtime.h"

// Baseline bring-up host. Multiplayer enhancements are not enabled yet.
int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    options.builtin_game_name = "Gundam SEED: Battle Assault (US)";
    options.builtin_rom_sha1 = "8287d3d336d94a85c5cb22da0fd84918199197e1";
    return gbarecomp::run_game(argc, argv, options);
}
