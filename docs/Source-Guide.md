# Source guide

The hand-written C++ port is formatted consistently and uses named GBA addresses.
The US menu and JP combat have separate namespaces because their memory layouts
and fighter IDs differ. The original address remains visible in the reference,
so a developer can move between source, IDA and cartridge evidence.

## Start here

| Area | Entry point | Responsibility |
| --- | --- | --- |
| Application | `src/main.cpp` | Parse portable inputs, install US hooks and start the host. |
| Launcher | `launcher/main.cpp` | Verify files, remap controls and launch/forward invitations. |
| US menus | `src/local_vs_runtime.cpp` | Route menu modes, update JP selection and lend the window to combat. |
| JP match | `src/jp_combat_runtime.cpp` | Initialize native JP Versus/Training and feed two player inputs. |
| Module boundary | `src/jp_combat.h`, `src/jp_combat_module.cpp` | Versioned contract between the menu executable and JP DLL. |
| Selection art | `src/jp_selection.cpp`, `src/jp_selection_overlay.cpp` | Compose the original JP roster and portraits. |
| Online room | `src/online_room.cpp` | Exchange names/selections and agree on a battle before handing off its socket. |
| Rollback | `src/jp_online.cpp` | Bootstrap deterministic JP state, run GekkoNet and restore/replay native frames. |
| UDP | `src/udp_transport.cpp` | Validate packet envelopes, pin the authenticated peer and borrow the existing socket. |
| NAT setup | `src/nat_traversal.cpp` | Discover the public endpoint and exchange AutoPunch-compatible probes. |
| Invitations | `src/invite_code.cpp`, `src/invite_window.cpp` | Validate shareable codes and deliver links to a fresh/existing game. |
| Controls | `src/live_controls.cpp`, `src/in_game_remap.cpp` | Independent seat inputs and F4 remapping policy. |
| Player identity | `src/player_profile.cpp` | Persist UTF-8 names and format live match titles. |
| SDL host | `src/host_window_sdl3.cpp` | Window, audio, gamepads, drawing and shared surface ownership. |
| Addresses | `analysis/address-map.json` | Authoritative names, values, meanings and evidence. |

## Addresses and layout

Read [Address-Map.md](Address-Map.md) for all named symbols. C++ includes
`game_addresses.h` and uses `seed::address::us`, `seed::address::jp` and
`seed::address::common`. Python tools import `Us`, `Jp` and `Common` from
`game_addresses`. Only the generated constant files contain executable raw
GBA address literals. Config files retain numeric hook entries required by the
recompiler, accompanied by human-readable notes.

For example, the JP online health report reads `jp_address::kPlayer1Health`.
The reference identifies that as the JP battle context plus 52, independently
of the US health location. Private hook/oracle return markers are named as
port allocations, not misidentified as original cartridge functions.

Change the JSON registry, then run:

```powershell
python tools/generate_address_maps.py
python tools/check_source_readability.py
```

Unidentified startup options have conservative names such as
`kPlayerStartupParameterA`; the reference states which values the port writes
and that their complete original meanings remain unknown.

## Guest function hooks

A hook runs at an original cartridge function entry. `return_from_guest_hook`
restores the caller's program counter and ARM/Thumb state. Short helpers are
called through `call_original_game_function`, which saves the CPU, supplies
register arguments and uses a private return marker to balance the dispatch
stack. These are guest addresses; they must never be treated as host pointers.

The US executable pauses while `SeedJpCombat.dll` borrows its SDL surface and
controls. Online combat also borrows the room's already-connected UDP socket.
The module owns one simulation containing both fighters. Rollback snapshots
include native machine state and JP hook metadata, while host UI, queued audio
and display names stay outside deterministic simulation state.

## Formatting and generated code

`.clang-format` defines the C++ style; `pyproject.toml` defines the Python style.
Generated address files are written by the generator and checked without a
formatter pass. Private generated cartridge/BIOS instruction output remains
outside Git and is not a full semantic decompilation. Dependency patches stay
as upstream-applicable diffs; their purposes are described in the README.
