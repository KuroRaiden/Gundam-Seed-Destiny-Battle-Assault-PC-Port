# Portable Windows release

The portable contains the launcher/game executables, JP combat module, SDL3,
C++ runtime DLLs, controller database, requested Haro/ship icons and license
notices. It excludes ROMs, BIOS images, game art caches, saves and player profiles.
The original v0.1.0 game-data download was withdrawn; current packages use the
runtime-only packager and archive audit.

Extract the whole ZIP to a writable folder and run `GundamSeedLauncher.exe`.
Put your own inputs inside `gamefiles/` using these prefilled relative paths:

- `gamefiles/Mobile Suit Gundam Seed - Battle Assault (USA).gba`
- `gamefiles/Kidou Senshi Gundam - Seed Destiny (Japan).gba`
- `gamefiles/gba_bios.bin`

`gamefiles/Game_and_Bios_Here.file` is a text placeholder listing the names.
The folder is shipped with only that placeholder. Use **Browse** for different
filenames or locations; it opens in the selected path's folder, falling back to
`gamefiles`. **Verify and save** checks the exact supported sizes/hashes.
An extracted 16 KB BIOS is required; ZIP archives are not accepted.

The original inputs are read in place. On first launch, selector art is generated
locally under `assets/`. **Prepare previews** in MS Colors similarly generates
its local preview cache. No Python, compiler or extra download is required for
normal play. Generated `.bin` caches remain local and are excluded from releases.

Saved paths inside the portable folder stay relative when it moves. External
selections retain their locations. Released settings contain only the shared
`gamefiles` defaults, never the developer's paths, controls, saves or colors.

Local VS replaces the US Password entry; Training uses the JP roster and engine.
F5 opens its frame meter without pausing, and F4 opens controls. Online VS uses
codes/links and keeps the same room for rematches. Names and title statistics
remain available. Both players should use the same updated build.

`tools/build_portable.py` copies an explicit runtime allowlist, the fixed
placeholder, the two approved icons and license notices. It accepts no game or
BIOS inputs. `tools/audit_release.py` checks manifest hashes, exact portable
defaults, marker content, branding hashes and known input-image hashes. It can
also scan every archive member against the developer's actual raw inputs.
Distribution format `seedpc-runtime-only-v3` identifies the new folder/default
policy; legacy v1/v2 packages retain their original blank-settings rules.

See [branding](Branding.md), [frame meter](Training-Frame-Meter.md),
[palette editor](MS-colors.md) and [license notices](../THIRD_PARTY_NOTICES.md).
