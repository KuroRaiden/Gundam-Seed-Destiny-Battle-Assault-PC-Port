# Portable Windows release

The v0.1.0 game-data download was withdrawn and removed. Use the corrected
runtime-only v0.1.1 release. It contains the launcher/game executables, JP combat
module, SDL3, C++ runtime DLLs, controller database and licenses. It contains no
ROMs, BIOS, extracted art pack, saves, player profiles or personal settings.

Extract the whole ZIP to a writable folder, run GundamSeedLauncher.exe and open
Game files. Select your own supported US Battle Assault cartridge, Japanese
SEED Destiny cartridge and extracted GBA BIOS. The launcher checks their sizes
and hashes. The original files are read in place and never modified.

On first launch the game uses the Japanese ROM to generate its selector art
locally under assets/. No Python, compiler or additional download is needed.
The native importer captures the original selector and decodes its donor
resources; its output is checked against the previously validated 91-image pack.
A generated .bin cache on the player's PC is normal; it must never be uploaded.

The launcher stores paths inside its folder relatively. External selections
remain absolute. Players may copy their own inputs into their local portable
folder if they want to move everything together. Release packaging always
starts with blank file paths and default settings.

`tools/build_portable.py` accepts runtime, launcher and CRT directories only.
It deliberately accepts no ROM or BIOS inputs and never copies app/assets,
user controls, saves or profiles. `tools/audit_release.py` checks the archive
allowlist, manifest, blank settings, known game/BIOS hashes and integrity. It
can additionally scan all member contents for explicitly supplied input images.
The release publisher audits ZIPs before creating a draft or uploading bytes.

Native Training, Local VS and online code/link play are unchanged. F4 remapping,
saved names and live title statistics remain available after local setup.
