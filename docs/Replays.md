# Match replays

Local VS and online battles record automatically into `replays/` beside the
launcher. Training is excluded. Each match in a continuous online room gets its
own file. A match left early is labelled **Partial recording**; an online partial
contains confirmed inputs rather than predictions.

Open **Replays** in the launcher, select a recording and choose **Watch replay**.
The list shows the recording date, players, mode, duration, fighters and stage.
**Refresh** finds files copied into the folder; **Open replay folder** opens it.
During playback, **Space** pauses, **Tab** fast-forwards and **Esc** returns to
the launcher. Gameplay controls cannot change either fighter. Watching does not
record another replay, change player palettes or connect to matchmaking.

Recordings require the same `SeedJpCombat.dll` build and BIOS. Incompatible or
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
