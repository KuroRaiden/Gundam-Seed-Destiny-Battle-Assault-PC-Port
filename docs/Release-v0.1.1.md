# v0.1.1 — Runtime-only distribution correction

The v0.1.0 portable incorrectly included user cartridge and BIOS files. Its ZIP
and checksum were removed from GitHub. This replacement contains runtime files
only: no ROMs, BIOS, extracted .bin art, saves, player profiles or personal paths.

After extraction, select your own supported US ROM, JP ROM and GBA BIOS in the
launcher's Game files page. The JP selection pack is generated locally on first
launch using the supplied Japanese ROM. No Python or compiler is required.

The packager uses an explicit runtime allowlist instead of copying local app
folders. A mandatory pre-upload audit rejects ROM/BIOS/cache/profile payloads,
old personal bundles, unexpected archive members and non-default file settings.
The manifest records that game data is excluded. ZIP and checksum are provided.

All v0.1.0 gameplay features remain: US menus, JP roster/combat, one-process
Local VS, Training, rollback online code/link play, remapping and player names.
