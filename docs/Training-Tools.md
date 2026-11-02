# Training tools

Enter Training from the US main menu, then choose both fighters and a stage.

| Key | Action |
| --- | --- |
| Start / Enter | Native pause menu, translated into English with the US ROM font |
| F2 | Save the current Training situation |
| F3 | Reload the saved Training situation |
| F4 | Remap controls; pauses offline play |
| F5 | Toggle the frame meter |
| F6 | Toggle collision boxes: red hitboxes, green hurtboxes |
| F7 | Save a clean PNG in `snapshots/` beside the launcher |
| F8 | Start Guard Cancel challenges / select the next drill |
| F9 | Retry the active challenge from its RAM checkpoint |
| F10 | End the challenge and restore your original Training situation |

F7 captures the combat sprites on a solid **#101C2B** background with the native
HUD and stage removed. The PNG uses the game window's size. Enabled F5 frame-meter
and F6 collision overlays remain visible; other menus and status messages are
omitted. Normal gameplay visuals remain unchanged. A paused capture uses the last
combat image before the pause menu; after loading a paused state, resume once
to refresh it. Snapshots are private local files, excluded from Git and packages.

The US pause-menu word sprites are extracted from the verified US cartridge at
launch into `assets/us-training-menu.bin`. This local cache is generated on the
player’s computer. Original JP navigation, setting values and pause behavior
remain authoritative. HP/PS labels follow the JP setting order.

## Guard Cancel challenges

F8 starts four drills: escape backward, escape forward, Guard Counter A, and
Guard Counter B. The selected opponent walks into range and attacks repeatedly
with its native B attack. Hold Back to block, then perform the displayed input:
Back + R, Forward + R, Forward + A, or Forward + B. Directions are relative to
your opponent; A/B/R refer to the game buttons under your chosen mapping.

Each drill has a five-success goal and an attempt counter. A successful cancel
must actually leave blockstun for the native escape/counter state and spend its
thruster cost. Early presses, ordinary attacks and the wrong technique do not
award progress. A hit or timeout offers a retry. F9 restores the prepared
situation immediately, including health, gauges, positions, timing and inputs.
F8 cycles drills; F10 restores the full Training situation from before the first
drill. These checkpoints remain in RAM and do not replace your saved practice
slot. F2/F3 are unavailable during a challenge; F5/F6/F7 remain available.

Challenges run only in offline Training. Their input script, host counters and
checkpoints are inactive in ordinary versus, netplay and replay playback.
Replay F8/F9/F10 retain their takeover controls. The challenge card is omitted
from clean F7 snapshots, which still include enabled frame-meter and hitbox overlays.

F6 reads native collision actors, including projectiles, their signed offsets,
facing flags, geometry counts and camera projection. Invulnerable hurtboxes and
disabled attacks are omitted. These are the game’s rectangles, without inferred
sprite silhouettes or a separate physics model. The viewer modifies a host RGB
copy only. Native RAM, framebuffer, palette and snapshots remain unchanged.

F2/F3 provide one persistent quick slot beside the local JP cartridge save,
with the suffix `.training.state`. They work during practice or its native pause
menu. Saving replaces that slot atomically; loading restores the complete CPU,
RAM, devices, timing, game settings and input state after the native dispatch
stack unwinds. Playback audio, collision presentation, outlines and frame-meter
history are cleared, and physical inputs wait for release.

Use states with the same game build, fighters, stage and selected/custom colors.
Incompatible or corrupt states are rejected before changing the live machine.
A missing slot displays a short message. State files are private player data;
they are excluded from Git and portable archives.

F2, F3, F6 and F7 are reserved and inactive outside offline Training, including online
matches. The tools do not alter the online protocol or require an opponent to
have them. F4 captures these keys while remapping so they cannot trigger a
Training action at the same time.

Validation covers all 27 fighters, native attack and projectile samples, exact
snapshot equality before/after display, repeated full-state restores, paused
states and rejected files. `seed_training_view_tests` checks signed coordinates,
mirroring, camera projection and native actor flags. SDL tests check key edges,
repeats, release, remapping isolation and online exclusion.

`seed_training_snapshot_window_tests JP_ROM BIOS OUTPUT` also exercises F7
through the production DLL's ordinary windowed Training loop. Host input can
arrive partway through a raster, so F7 must finish that in-progress capture
rather than require an idle frame boundary. This check reproduces the original
zero-file failure and verifies three saved PNGs, including F5/F6 overlays.
