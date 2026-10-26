#include "runtime.h"
// Build-time donor inspection only. The driver requires the reference CPU
// backend; this is never used as a second game instance by the PC port.
int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    options.builtin_game_name = "SEED Destiny donor asset reference";
    options.builtin_rom_sha1 = "64eea52cd909669a972c091869c5f47865863bc0";
    return gbarecomp::run_game(argc, argv, options);
}
