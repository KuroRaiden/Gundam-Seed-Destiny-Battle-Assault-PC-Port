# v0.1.0 — Portable online release

The first Windows x64 portable release combines the US menus with the full JP
Destiny roster, selection art and native JP combat. Local VS runs both players
in one simulation; Training uses the JP practice mode. The original menu's VS
Mode opens the ImGui online window.

- Share SEED1 codes or invitation links; address discovery and native
  AutoPunch-compatible hole punching select the connection automatically.
- GekkoNet rollback, two frames of input delay, F4 remapping during play and
  live player-name/delay/rollback/ping titles.
- Saved UTF-8 player names, keyboard/gamepad remapping, SDL3 and controllerdb.
- Portable relative file paths, application-local C++ runtime and SDL DLLs.
- Readable hand-written source, 153 named address/layout symbols, a source guide
  and a generated US/JP address reference with evidence and uncertainty notes.
- Removed the SuperCombo/Mednafen fallback configuration.
- Fixed successful match teardown sending a room-leave message during the
  opponent's final checksum acknowledgement window. Explicit cancellation still
  sends its leave notice.
- Fixed an online startup ordering issue by repeating host readiness with the
  pending start offer until the guest acknowledges it.

Automated validation covers original US input and JP asset decoder comparisons,
local selection, controls/settings, room/NAT fixtures, production JP rollback,
name exchange, live title stats and portable relocation/invitation launching.
The user separately confirmed a synchronized two-PC online smoke test. Its
exact NAT route was not instrumented. Some guest code still uses GBARecomp's
interpreter bridge; complete static coverage and exhaustive matchup/long-session
coverage remain unfinished. Direct connections can fail on networks that block
hole punching; no fallback relay is configured.

Extract the complete ZIP to a writable directory and run GundamSeedLauncher.exe.
Optional invitation-link registration can be enabled in Online VS; enable it
again after moving the portable folder. Both peers should use this release.

Source references: [Source guide](https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/blob/v0.1.0/docs/Source-Guide.md),
[named US/JP addresses](https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/blob/v0.1.0/docs/Address-Map.md).

**Withdrawn download:** the original portable ZIP and checksum were removed
because they included user game/BIOS inputs. Use v0.1.1, which contains runtime
files only and creates its selector art locally from user-supplied files.
