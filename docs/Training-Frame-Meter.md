# Training frame meter

F5 toggles a passive two-player frame meter during JP Training combat. It is
hidden by default. It neither pauses gameplay nor captures normal game inputs.
F5 remains available to the existing remapping overlay while F4 is open.
Local VS, Online VS, character selection and the US menus cannot enable it.
The module uses no external injector or additional cartridge file.

Green cells are startup, red active, blue recovery and yellow hitstun/damage
reaction. Damage reaction includes native airborne knockdown; it is not a
promise that every yellow frame prevents a character-specific recovery option.
Grey cells distinguish idle and gaps between hits; white indicates hitstop.
A thin separate red track represents a live projectile so its continued flight
does not turn its owner's recovery red. Each column is one native battle update.
The last sequence stays visible after twelve idle frames; a new action starts
a fresh sequence. The ring retains at most 240 frames and displays up to 120.

The startup number includes the first active frame: twelve green cells before
red means Startup 13. A move with no observed active frame shows --. Counts
exclude hitstop. Interrupted moves are marked. No on-block advantage, cancel
advantage, invulnerability or damage numbers are fabricated. The convention
follows [Dustloop's frame-data guide](https://dustloop.com/w/Using_Frame_Data).
The user also supplied [the SF6 frame-meter video](https://www.youtube.com/watch?v=5mFQAQVg2zg&t=28s)
and a sample chart; direct video contents could not be retrieved by the browser.

The JP frame-begin hook reads slowdown before native code decrements it.
The tail hook reads both fighters after animation/collision updates. Both
callbacks return zero, preserving original execution and CPU registers. Only
Training enables them. The reader validates guest pointers, ignores hurtboxes
and disabled/zero-size colliders, and reads native animation hitbox records.
All writes are to bounded host history. Guest RAM, ROM, canonical snapshots,
combat ABI and online wire format are unchanged. F4/native pause does not
advance the battle callbacks. Rendering uses an isolated ImGui context, with
renderer state restored before the controls overlay draws above it.

Validation includes shooting and melee for all 27 native fighters (56 attack
cases including beam/whiff checks), plus final continuation-tracking checks on
fighters 1, 13 and 27. Native damage reactions produce the opponent's yellow
track, and showing/hiding the meter leaves guest RAM unchanged. The native
pause test suspends an attack sequence and confirms it resumes afterward.
The native SDL test checks F5 open/close/repeat/release, F4 isolation, no pause
or save-state shortcut leakage, VS/online exclusion and all four rendered colors.
F5 show/hide was also checked in the real shared US-menu/JP Training window.

The timeline and reader unit tests cover gaps, hitstop, interrupts, cancels,
animation continuations, history bounds and invalid pointers. Two production
online matches still converged with the same invitation and zero desyncs.
Special-move, throw, cancel-window and recovery-option coverage is not exhaustive;
these live measurements are not an authored move database or a frame-advantage
calculator. See `validation/training-frame-meter.json` for the tested scope.

The generated JP cartridge source must be refreshed from the updated
`game-jp-combat.toml` before a full build so the two new entry guards exist.
`seed_training_frame_meter_tests` checks timeline behavior;
`seed_jp_frame_probe_tests` checks the read-only native layout adapter.
`seed_frame_meter` is an opt-in debug TCP command (toggle/clear/status) for the
repeatable native qualification. Normal play opens no debug listener.
