# Match replays

Playback progress, pause status and controls appear in the native window title;
no replay status strip covers the game HUD. These replay tools also accept
recordings from the original 0.1.8 release. BIOS and initial/final machine-state
checks remain enforced.

Local VS and online battles record automatically into `replays/` beside the
launcher. Training is excluded. Each match in a continuous online room gets its
own file. A match left early is labelled **Partial recording**; an online partial
contains confirmed inputs rather than predictions.

Open **Replays** in the launcher, select a recording and choose **Watch replay**.
The list shows the recording date, players, mode, duration, fighters and stage.
**Refresh** finds files copied into the folder; **Open replay folder** opens it.
During playback, **Space** pauses, **Tab** fast-forwards, **F6** toggles native
hitboxes/hurtboxes and **Esc** returns to the launcher. Collision boxes also work
while paused and during takeover. They read the current frame without changing
recorded inputs or machine state. Watching does not record another replay,
change player palettes or connect to matchmaking.
Red boxes mark attacks and green boxes mark hurtboxes. The Training legend and
save/load shortcuts remain exclusive to Training.

**Replay takeover:** press **F8** to control 1P or **F9** to control 2P at the
current frame. Either choice uses your existing P1 keyboard/controller mapping;
the opponent continues its recorded inputs. This starts an offline practice
branch. **F10** restores the takeover checkpoint for another attempt. F8/F9
while practicing switch sides and restart from that same checkpoint. **F11**
restores the checkpoint and resumes the original recorded sequence. Space and
Tab still pause/fast-forward. Practice pauses when the round or recorded segment
ends, allowing retry or return to playback. Takeover shortcuts are ignored while
F1/F4 menus are open. The checkpoint stays in memory; the original replay file
is never rewritten, and practice attempts are not automatically recorded.

Recordings require a compatible `SeedJpCombat.dll` build and the same BIOS. Incompatible or
damaged recordings are rejected with a message. Keep the corresponding portable
build if you want to watch older recordings after a game update. The launcher
updater preserves the `replays/` folder.

The `.seedreplay` format contains fighter/color/stage choices, custom palettes,
player names, a UTC recording time, compatibility hashes, initial/final machine
checksums, initial runtime scheduling counters and two button masks per frame. It contains no ROM, BIOS, machine
snapshot, saved progress, original file path, IP address, invitation or token.
The file has a format version, strict bounds, validated fields and a file CRC.
Files are written to a unique pending name and renamed only after a complete
write. Personal recordings are ignored by Git and excluded from portable ZIPs.

Online recording uses GekkoNet's confirmed-input recorder with embedded machine
state disabled. Its trusted output is converted to the same input-only format
used by local matches. Recording stays outside rollback snapshots and never
changes packets, input delay or the shared match state. Disk failures are logged
and do not end the online match. Final checksums are verified for complete
recordings; partial recordings verify their starting state.

Versus matches and playback boot from an isolated temporary cartridge save and a
fixed emulated clock. Native stepping stops before the round intro, preserving
the full match start. Training continues using its existing save/state path.

Validation targets: `seed_replay_tests` checks malformed files, metadata,
palettes and file preservation; `seed_replay_native_tests` records/replays real
JP battles and compares full machine checksums. Production online peer tests
also produce recordings for playback qualification.

`tests/replay_integration.py --build <Release folder> --rom <JP ROM> --bios <BIOS>
--out <new scratch folder>` runs the format/native cases, two production UDP
peers, playback in fresh processes, viewer pause/F1/F4 checks and an interrupted
match whose confirmed partial recording must still play. It reads original
inputs in place and writes fixtures only inside the supplied output folder.

`tests/replay_practice_integration.py --build <Release folder> --replay <480-frame-or-longer replay>
--rom <JP ROM> --bios <BIOS> --out <new scratch folder>` checks both takeover
seats, exact checkpoint retries, F1/F4 isolation, return to the original match,
clean practice exit and end-of-segment handling. It also compares F6 on/off
captures while checking the original machine checksum and file hash. The
developer-side image comparisons require Pillow; the portable does not.
