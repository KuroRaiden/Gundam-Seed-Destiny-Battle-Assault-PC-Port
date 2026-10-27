# Local VS

Launch `app/GundamSeedLauncher.exe` and choose **Local VS**, at the bottom
right of the US main menu. Both players select independently on the Japanese
Destiny grid, then fight with the original JP combat engine. All 24 visible
grid choices are available. One process and one game window run one advancing
cartridge at a time. No link cable or second game instance is required.

Move with the D-pad/stick and press **Start** to cycle colors/hidden variants
while unready, then press GBA **A** to ready. Each local seat uses its own
Start binding. Training uses P1 controls for whichever fighter is being chosen.
The footer shows each color number. Returning from a match keeps the colors.
 **B** unlocks a ready
player; B while unready returns to the US menu. Both ready starts JP combat
after a short initialization. Match completion clears both ready flags and
returns to the same selector. **Esc** during combat also returns to selection.
Closing the game window exits the game.

| Action | P1 default keyboard | P2 default keyboard | Default gamepad |
|---|---|---|---|
| Directions | Arrows | I / J / K / L | D-pad / left stick |
| GBA A / ready | X | Numpad 1 | South |
| GBA B / back | Z | Numpad 2 | East |
| Start | Enter | Numpad Enter | Start |
| Select | Right Shift | Numpad 0 | Back |
| GBA R | V | Numpad 6 | Right shoulder |
| GBA L | C | Numpad 4 | Left shoulder |

The launcher **Controllers** page configures each seat's keyboard, assigned
gamepad slot, primary buttons and alternate stick/trigger directions. Click to
capture a binding, right-click to clear, then **Save bindings**. Reset and revert
are available. Saved profiles apply to US menus, local selection and JP combat
on the next game launch. `controls.ini` stays beside the executable and builds
do not overwrite it. Slots follow connection order; unplugging a controller
preserves the other seat. `controllerdb.txt` is loaded by both programs.

The main menu's **VS Mode** opens an ImGui online lobby over the JP selection
grid. The original US cable Connecting screen is bypassed. Direct UDP hosting
and joining use copyable invitations and GekkoNet rollback. A disconnected
lobby cannot start a match. The bottom-left Preset VS entry is not intercepted.

**Training Mode** also opens the full JP grid. Use P1's controls to choose your
fighter, press A, then choose the practice opponent and press A again. B during
opponent selection returns to your fighter; B at the first choice returns to
the main menu. The native JP training engine keeps its practice settings:
Start opens/closes practice options, Select resets practice, and Esc returns
to the JP selector. F4 remapping pauses offline training as it does Local VS.

## Verification

`tools/verify_jp_local_vs.py` navigates the real title and US menu into Password
Mode, confirms two JP-only suits independently, starts JP combat, verifies P2-only
movement and damage, executes two KOs, checks rematch flags, reuses the module
for a second match, and returns to the US menu. A temporary low-health fixture
shortens each KO; real P2 attacks and the original round transitions execute.

```powershell
python tools/verify_jp_local_vs.py --exe app/GundamSeedRecomp.exe --rom 'path/to/US.gba' --jp-rom 'path/to/JP.gba' --bios 'path/to/gba_bios.bin' --out '../../work/jp-local-check'
```

The [integration report](../validation/jp-local-vs/report.json) and screenshots
record that run. `verify_jp_roster.py` loads all 24 original grid choices in
both seats and exercises movement/A/B/L/R; this is smoke coverage, not an
exhaustive 24-by-24 matchup test. Native window checks also verified the borrowed
renderer, P2 inputs, Esc return and closing. See [the combat integration](JP-combat.md).

The older `verify_local_vs.py` full-match path documents the superseded US
combat experiment. Its `--entry-probe vs|preset|password` routes remain useful.
