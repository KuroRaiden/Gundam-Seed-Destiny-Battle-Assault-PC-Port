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

`config/brand-icons.json` records the only two ROM-derived art files permitted in portable
packages and their SHA-256 hashes. Distribution formats seedpc-runtime-only-v2/v3
require those exact files when branding is included and prohibits game asset caches. The legacy v1
format still permits no art. The packaging test rejects extra ROM/BIOS/cache
files and modified icons even if their manifest hashes have been rewritten.
The requested icons do not authorize packaging cartridge, BIOS, selector or
color-preview data; those continue to come from the player's own files.

The launcher sidebar decodes the original Japanese SEED Destiny title logo in
memory after verifying the user's Japanese ROM. Group 2 resources 13, 14 and 15
provide its 4bpp tiles, 30x20 tilemap and palette. The transparent logo is trimmed
to its visible bounds. No ROM, title image or generated title cache is bundled
or committed. Without a verified Japanese ROM, the text heading remains available.

`Themes/` contains Default, Earth Alliance and ZAFT palettes and the two faction
emblems supplied by the user. These separate assets are pinned by
`config/launcher-themes.json`; the release audit rejects extra or modified theme
assets. Choose a palette under **Play > Launcher theme**. The choice is saved in
the existing launcher settings, and theme changes affect only the launcher.
Earth Alliance uses the emblem's cyan/blue and green; ZAFT has white backgrounds
with the emblem's red/blue accents.
The in-game palette editor, technical overlay colors and snapshot background
are independent of these themes. Updates preserve the theme preference and any
extra personal theme files. Older updaters without support for `Themes/` require
manual ZIP extraction for this first theme-enabled build.
