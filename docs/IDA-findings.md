# Gundam SEED PC port: verified IDA findings

Analysis date: October 25, 2026. Base: US BGNE revision 0, SHA-256
`d7952790a4b82b6d230a4953e4bfe48f6693ddd550f1cf0e7ace74bce95453f8`.
Japanese donor: B42J revision 0, SHA-256
`cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb`.

## Analysis setup

Both databases use ARM32 little endian, ROM mapped at `0x08000000`, ARM and Thumb
interworking, and the GBA RAM and hardware-register segments. The initial
automatic raw-file import incorrectly selected x64. Those initial sessions were
closed; the research uses separate workspace copies and corrected ARM databases.
The original ROM bytes were not patched. Names below are research annotations.

## True local versus

Runtime menu probes distinguish the actual main-menu entries: Password is
column 2, row 2 and enters state 7 without assigning a new game mode. Preset VS
is column 0, row 2 and sets mode 6. The earlier identification of mode 6 as the
Password entry was incorrect. The native input hook now qualifies the observed
main-menu transition by its Password cursor position before redirecting it.
Mode 6 is retained only as the local battle rules. VS is column 1, row 0; the
unmodified entry sets mode 1 and reaches state 12's cable Connecting screen.
The native host now redirects that qualified transition into an ImGui online
lobby over the JP selector. Connection transport remains unfinished.

The US engine already has distinct player input state and a human-controlled
second fighter. Its link path is a source of controller values and session
commands, rather than a requirement for a second fighting simulation.

| Location | Verified behavior |
|---|---|
| `0x080003A0` | Thumb main loop. Reads `KEYINPUT`, converts to active-high button bits, and dispatches game states. |
| `0x08000418`–`0x08000438` | When link status equals 5 and game state equals 9, supplies two received values to the two input routines. Other paths update player 1 from the local keypad. |
| `0x08000860` | Updates player 1 held/previous/pressed/released, repeat counters, and input history. |
| `0x08000934` | Corresponding independent player 2 input update. |
| `0x03000F48` | Main game-state index. |
| `0x030000B0` | Substate index. |
| `0x030075D0` | Game-mode byte; mode 1 takes link-specific character-select paths. |
| `0x03007610` | Link-status byte; value 5 enables the battle's player 2 human-input branch. |
| `0x03007600`, `0x03007604` | Pointers to the two received controller/command values. The values are state dependent. |
| `0x080A3F5C` | Main game-state function table. |
| `0x080022BC` | State 9 battle dispatcher. |
| `0x0800242C` | Battle update. Copies player 2 input into fighter 2 when link status is 5; other modes can run AI. |
| `0x0802A660` | State 11 character-select dispatcher. |
| `0x080AEB2C` | Character-select substate table. |

The battle context is based at `0x03000500`. Fighter records begin at context
offsets 52 and 152. Held/pressed/repress values are copied into context offsets
76/84/80 and 176/184/180 respectively. These are guest addresses and layout
facts, not native host pointers.

The character-select context is also based at `0x03000500` while that state is
active. It stores cursor positions at offsets 75/76, selected fighter indices
at 77/78, and control substates at 86/87. `0x0802A890` initializes both selections.
The two-sided command reader is character-select substate 27 at `0x080338C0`:
it reads both received-value pointers and chooses their order from context
offset 87. Substate 22 at `0x0803123C` also builds selection command words.
The original link command routines remain useful references. The local mode
instead replaces control substate 3 with a native two-player coordinator;
see [the implementation notes](Local-VS.md).

The important integration constraint is semantic: character selection sends
commands such as `0x2600 | selected_index` and has handshake phases. Simply
pretending a cable is connected and copying held-button masks into the received
buffers will not replace that protocol. Local VS needs a local selection
coordinator and direct two-player input routing, with the cable initializer and
serial waits bypassed. The same controller streams should later feed rollback.

## Native code implemented and tested

`src/input_state.cpp` implements the stock input-state behavior for two independent
players in one `MatchInputs` object. It has no clock, network, SDL, or global state.
All its state must be captured with the match for rollback.

`tests/input_oracle.cpp` executes the original US Thumb routines at `0x08000860`
and `0x08000934` in the ARM interpreter, and compares their memory results with
the native implementation. It passed 4,096 frames for both players (8,192 original
routine calls), including long holds, independent inputs, randomized masks,
repeat timing, all history entries, and masking of non-GBA input bits.

The running local VS mode now routes both controller streams through this
component, writes the verified guest input records, and supplies P2 masks to
the original battle remapping routine.

## Japanese fighter transplant

The supplied Japanese ROM identifies as **SEED Destiny**, not the same US game
with a regional text change. IDA confirms incompatible battle-record layouts:

| Detail | US BGNE | Japanese B42J |
|---|---|---|
| Battle initialization | `0x0800231C` | `0x08002DC0` |
| Fighter record start offsets | 52 and 152 | 46 and 142 |
| Fighter record stride | 100 bytes | 96 bytes |
| Fighter ID bit field | low 4 bits | low 5 bits |
| Selected fighter globals | `0x03007620`/`0x03007621` | `0x030075F0`/`0x030075F1` |

These widths do not establish a roster count by themselves. Copying the Japanese
fighter structure or code directly into the US engine would use the wrong
offsets. Each donor fighter's moves, collision data,
animations, graphics, palettes and sound references must be adapted to US formats.
The scope is now the full Japanese roster and selection screen. A verified
selection-resource importer is implemented; see `JP-selection-import.md`.
The fighter behavior transplant remains unimplemented.

## Remaining validation

The reproduced call-return overflow near `0x0804506A` came from return records
retained after IRQ completion. The saved runtime patch restores the entry depth
after a successful IRET; a normal-BIOS 2,400-frame run and the local VS integration
flow now pass. Missing static coverage still invokes the interpreter bridge.
The native JP selection compositor now uses the original grid and multipart
mech/pilot/name assets with independent local cursors. Physical controller
coverage and JP-only battles are now implemented and smoke-tested; see
`JP-combat.md`. Native rollback and code/link connections are implemented;
see `Online-VS.md` for validation and current limits. No fallback relay is configured.
