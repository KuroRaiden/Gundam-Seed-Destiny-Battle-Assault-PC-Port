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

This is the **simplified control** reference. It does not document Manual-control
motion commands. Functional command/air-availability facts and mode categories
reference [kira_yamato_88's guide v1.68](https://gamefaqs.gamespot.com/gba/924564-gundam-seed-destiny/faqs/34177).
Descriptions, ratings, and strategy text are omitted. This is not an exhaustive
game command dump.

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
