# F1 movelist

F1 automatically opens the fighter you chose on character select, including
alternate pilots. For example, choosing Sword Impulse for Training opens Sword
Impulse. Online joiners default to their own fighter. Player 1
and Player 2 buttons select the current match's native fighter. The dropdown
also allows browsing another suit without changing the match selection.

Inputs use Kenney Nintendo Switch D-pad glyphs and the game buttons A/B/L/R.
They describe logical GBA commands, regardless of keyboard/controller remapping.
Directions assume the fighter faces right. A plus means simultaneous inputs;
`>` separates a follow-up. `(AIR OK)` means usable on the ground or in the air;
`(AIR)` means airborne only. MS/MA and Berserker labels specify required states.
Each character lists Normal Berserk, SEED Mode, or Newtype Mode.

Each fighter also has a **Guard cancels** section, using the same D-pad/button
glyphs: Back + R or Forward + R to escape, Forward + A and Forward + B for the two
counter attacks. These require an active block and sufficient thruster gauge;
they work in ground or air guard. Inputs are shared across the roster, while
counter animations come from the selected suit's native animation table.

The commands were checked against JP ROM routines `0x080060C4`, `0x08008B1C`
and `0x08008D1C`, and executed in the native Training engine. The shared dispatcher requires
guard state 3/4, a nonzero guard timer and animation frame; the handlers test
new R plus a direction, or held Forward plus new A/B, and spend 3584 thruster units.

This is the **simplified control** reference used by the port. Commands now come
from the supported Japanese game's ground/air command records and cancel routes,
not the FAQ. Manual-control motion commands are outside this reference. Basic
attacks and distinct follow-up actions are included. A follow-up uses the native
cancel window and may require contact; it is not a guaranteed combo.

Native portrait IDs determine the suit and pilot. The old reference mistakenly
assigned several FAQ-order names to native IDs: ID 10 is Red Frame, ID 11 is
Freedom, ID 20 is Slash Zaku, ID 21 is Providence, and ID 22 is Gold Frame.
Those mappings and the other misplaced entries are corrected. Berserker types
come from the native fighter descriptor and dispatcher; Savior uses Normal
Berserk in this game. Gaia has separate MS/MA lists. Transforming with L changes
its command descriptor, so a follow-up from the previous form is not inherited.

`tests/jp_command_catalog.cpp` reads the player's original cartridge in place and
stores its research catalog outside the checkout. `tools/export_native_movelist.py`
exports only functional button/availability facts to `src/native_move_inputs.inc`.
It does not copy ROM bytes, sprites, animation data, Manual command sequences or
decompiled game code. The native ground/air dispatchers are `0x08007A40` and
`0x0800779C`, the input matcher is `0x0800B150`, and the Berserker dispatcher is
`0x0800935C` (fighter descriptor byte 42).

`seed_native_movelist_tests JP_ROM BIOS PRIVATE_OUTPUT` executes the displayed
inputs for all 27 fighters and both Gaia forms. It checks the accepted native
motion/route, ground/air availability and Berserker category; follow-up tests
use close contact rather than assuming on-hit cancels work on whiff. The UI
test checks glyphs, selection, offline pause and online continuation.

Offline F1 pauses simulation and audio using the existing F4 host pause
mechanism. Online simulation continues and the local player's gameplay inputs
are neutral while browsing. Closing requires input release before gameplay
resumes. Presentation metadata does not read or write guest memory, enter a
rollback snapshot, change match negotiation, or affect the peer's display.

The eight original PNGs come from the user-supplied Kenney Input Prompts 1.5A
pack (CC0). Four are D-pad directions; four are A/B/L/R. They ship under
`resource/input-prompts/` with a notice in `resource/licenses/`. The portable
audit pins their SHA-256 hashes and continues to reject ROMs, BIOS images,
game art caches, personal settings, and unrelated image files.
