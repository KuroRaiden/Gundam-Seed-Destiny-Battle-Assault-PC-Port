# JP combat behind US menus

The approved design uses US BGNE menus and JP B42J combat. The ROMs remain
unchanged. `SeedJpCombat.dll` contains the separately generated JP cartridge
and its runtime globals; it never launches another process. During a match
the US runtime waits. Only JP combat advances, borrowing the US SDL3 window
and renderer. The menu's audio stream pauses and resumes on return. Shared
`SDL3.dll` keeps the borrowed objects valid across both modules.

The original 24 grid choices map to native JP IDs 1–23 and 25 (host selector
IDs 0–22 and 24). JP-exclusive suits use their actual battle routines and
assets. Start selects both original palettes for every suit and all three hidden
variants. Native IDs 24, 26 and 27 are now available through their original
roster slots. Standard balanced OS/stat values are taken from the donor
selection defaults; there is currently no per-player OS-settings screen.

| JP address | Purpose |
|---|---|
| `0x03000CBC` / `0x03000DDC` | Main state / battle substate |
| `0x03000CD0` | Battle context; fighter records +44 and +140, stride 96 |
| `0x03000C40` / `0x03000C78` | Original P1/P2 input records |
| `0x080025C0` | Original held/edge/repeat/history input routine |
| `0x08002654` | Hook: update both native input records |
| `0x08003418` | Hook: replace P2 AI selection with local input |
| `0x08003C78` | Original copy from input record to fighter actions |
| `0x0800595C` | Original GBA A/B/L/R button mapping |

Battle uses standard non-link state 4. P2's AI/serial selector is replaced
with an original input-copy call, while all combat logic runs unchanged.
Main state leaving 4 completes the match session and returns to the US-hosted
JP selector with both players unready. The entry/exit hooks balance the native
generated return ABI. Native return records stayed at depth zero at all
sampled validation boundaries.

## Validation and limits

- Native US Password → JP-only choices → battle → P2 movement/damage → two KOs
  → rematch → second match → US menu passed in one process.
- Every original grid choice loaded in P1 and P2 and handled movement/A/B/L/R.
  Neutral-input checks confirmed P2 did not act as a CPU.
- Direct3D11 shared-window presentation, audio resume, Esc return and window
  close were exercised. SDL renderer pixel/audio/hotplug tests also passed.
- Saved per-player keys/buttons/axes, invalid-profile guards, Unicode paths
  and two SDL virtual gamepads passed. Launcher key capture and save were
  exercised through the actual UI.

These checks do not establish exhaustive move, matchup or long-session
compatibility. Some paths still use GBARecomp's interpreter bridge rather
than static output. Online VS uses the JP-backed ImGui lobby, production GekkoNet rollback,
shareable codes/links and native AutoPunch-compatible hole punching.
See `Online-VS.md` for validation and remaining Internet coverage.

## Training

US Training Mode (menu column 2, row 0, state 11) shares the JP selector, with
P1 choosing both the player and practice opponent. `JpCombatRequest` API version
7 distinguishes versus and training and carries explicit colors/custom palettes. Training enters JP state 8, substate 0;
the native initializer at `0x0802579C` establishes practice settings, followed
by `0x080257E8` for fighter/stage setup. The `0x080030DC` dispatcher calls the
practice handler at `0x080258B4` in state 8. The P2 input hook is bypassed for
training, preserving the native dummy behavior and practice options rather
than forcing a second local human. Versus still uses state 4.

Native menu integration checks verify JP-exclusive fighters in both training
seats, one-controller selection, practice idle, Start options/resume, Select
reset and return to the JP selector/US menu. The roster probe accepts
`--training` to load all 24 choices in both seats.

Quick Enter taps could disappear if SDL drained both key-down and key-up in
one host pump before the level-state sampler ran. `LiveControls` now retains
mapped keyboard/gamepad button presses for one host frame and exposes that
same pulse to the menu and JP callback. Release guards also clear pending
pulses, so remapping/lobby dismissal cannot trigger an accidental confirm.
The regression failed before this fix and passes afterward. Real Enter presses
were also verified to open native training substate 4 and resume substate 2,
with scripted inputs disabled. Three additional windowed pause/resume cycles
continued advancing frames. Interactive combat handoff clears developer menu
input overrides so they cannot suppress normal controls during manual testing.

`runtime-debug-stop.patch` fixes a Windows test-observer shutdown stall with
an idle connected client. Normal launcher play opens no debug listener.
`seed_debug_stop_tests` reproduces that lifecycle without a game window.

## HUD palette correction

The BIOS HLE CpuFastSet helper copied only the requested word count. The
original BIOS rounds a partial transfer up to a full eight-word block. JP HUD
initialization requests four words for several 32-byte palettes, so HLE left
half of those palettes blank, producing black name/header and bottom HUD areas.
`runtime-cpufastset-rounding.patch` restores full blocks and charges nominal
copy timing for that block size. `seed_bios_fast_set_tests` compares 30 copy/fill
cases directly with the supplied BIOS instructions, including zero, partial
and complete blocks. Destination bytes and sentinel tails match. The native
HUD was also compared with the recompiled-BIOS path.
