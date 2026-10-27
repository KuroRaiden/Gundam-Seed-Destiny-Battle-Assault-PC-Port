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
locally under `assets/`, along with stage previews and the US Training font.
**Prepare previews** in MS Colors similarly generates
its local preview cache. No Python, compiler or extra download is required for
normal play. Generated `.bin` caches remain local and are excluded from releases.

Saved paths inside the portable folder stay relative when it moves. External
selections retain their locations. Released settings contain only the shared
`gamefiles` defaults, never the developer's paths, controls, saves or colors.

Local VS replaces the US Password entry; Training uses the JP roster and engine.
Stage select offers 25 normal JP stages plus Random. Training has an English
pause menu using the US ROM font, F5 frame meter, F6 hitbox/hurtbox viewer,
and F2 save / F3 load for practice situations. F4 opens controls. Online VS
uses codes/links and keeps the same room for rematches. Names and title
statistics remain available. Both players need the updated build for shared
stage selection.

`tools/build_portable.py` copies an explicit runtime allowlist, the fixed
placeholder, the two approved icons and license notices. It accepts no game or
BIOS inputs. `tools/audit_release.py` checks manifest hashes, exact portable
defaults, marker content, branding hashes and known input-image hashes. It can
also scan every archive member against the developer's actual raw inputs.
Distribution format `seedpc-runtime-only-v3` identifies the new folder/default
policy; legacy v1/v2 packages retain their original blank-settings rules.

See [Training tools](Training-Tools.md), [branding](Branding.md), [frame meter](Training-Frame-Meter.md),
[palette editor](MS-colors.md) and [license notices](../THIRD_PARTY_NOTICES.md).

## Sprite outlines

On the launcher's **Play** page, **Black sprite outlines** switches the one-pixel
black fighter border on or off. **Outline opacity** sets its strength from 0%
(invisible) to 100% (solid black). Both preferences save automatically and apply
when launching. Older settings default to enabled and 100% opacity. These only
affect your display, so opponents can use different settings without affecting
rollback.

## Launcher version and updates

The bottom left displays the version embedded in the launcher. **Updates** checks
stable releases from this project's GitHub repository. **Download update** runs
in the background and verifies the ZIP against GitHub's SHA-256 digest, then
checks each runtime file against the portable manifest. **Install and restart**
waits for the launcher to exit, replaces the application files in this folder,
and reopens the launcher. Close the game and other launchers in that folder first.

Updates preserve `launcher-settings.ini`, controls, names, MS color profiles,
saves, Training states, game files and locally generated art caches. Fresh-install
settings in the download do not replace an existing settings file. Only explicitly
allowed application files, bundled libraries, documentation and license notices
are updated; other local files are left in place. Normal replacement failures
restore the previous application files from `.updates/<job>/backup`. The backup
and installer log remain available there, including after an interrupted update.

Existing 0.1.3 builds need a one-time manual installation of a build containing
this updater. The new launcher is currently **0.1.4-dev**, ahead of the published
0.1.3 fixed release, so it will offer the next compatible stable release once
published. Checks use the public GitHub API without credentials and can be
rate-limited; there are no periodic background checks.

Development uses `config/version.txt` as the launcher version. The build writes
`launcher-build.json` alongside the executable; the portable packager verifies
that receipt so the displayed version and package version cannot disagree.
