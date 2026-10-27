# Original game branding

The launcher uses the ship from the user's US difficulty-screen screenshot.
The game uses the orange Haro from the Japanese shop screenshot. Each design
is used for both its executable file icon and its native SDL3 window icon.
Original colors, transparent backgrounds and pixel edges are preserved.

The JP shop is native state 29. Haro uses 4bpp OBJ tile 882, palette 5, with a
32 by 32 texture (32 by 26 visible). The ship comes from the original 8bpp BG0
plane in US difficulty state 17, substate 6, with 123 by 124 visible pixels.
`tools/generate_brand_icons.py` reproduces both ICOs from local native VRAM,
palette and IO captures. These private raw captures are not release inputs.

`resources/game-icons.rc` embeds Haro as icon group 101 in the game.
`resources/launcher-icons.rc` embeds the ship as group 101 in the launcher.
Each ICO has 16, 24, 32, 48, 64, 128 and 256 pixel sizes using nearest-neighbor
scaling. `src/brand_window_icon.h` contains the two generated 32 pixel images;
`src/brand_icons.h` applies the correct one to each native window. JP combat
retains Haro when borrowing the main game window.

`tools/verify_brand_icons.py` checks every embedded resource bitmap against
its approved original. The native SDL host test reads WM_GETICON pixels for
the game's Haro and the same ship-setting helper used by the launcher.

`config/brand-icons.json` records the only two art files permitted in portable
packages and their SHA-256 hashes. Distribution formats seedpc-runtime-only-v2/v3
require those exact files when branding is included and prohibits game asset caches. The legacy v1
format still permits no art. The packaging test rejects extra ROM/BIOS/cache
files and modified icons even if their manifest hashes have been rewritten.
The requested icons do not authorize packaging cartridge, BIOS, selector or
color-preview data; those continue to come from the player's own files.
