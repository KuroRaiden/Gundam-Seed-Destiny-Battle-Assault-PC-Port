# Native launcher

The launcher uses Dear ImGui and SDL3. Select and verify the US cartridge,
Japanese cartridge and extracted BIOS on **Game files**, then launch from
**Play**. Supported sizes and hashes are checked again before launching.
The launcher hides during play and returns with exit/error reporting and a
shortcut to `Logs/last-game.log`.

**Play** has one **Launch game** button. Game mode selection stays in the
game's main menu; the launcher has no Local VS/Online VS cards or Online page.
Game files, Controllers and the display setting under Play remain
available.

**Replays** lists automatic Local VS and online recordings from `replays/`.
Select a match and choose **Watch replay**; Space pauses, Tab fast-forwards and
Esc exits. Recordings keep both fighters' inputs, colors and stage. See
[replays and compatibility](Replays.md).

## Remapping

Open **Controllers**, select **Player 1** or **Player 2**, and click a keyboard,
gamepad or alternate binding. Release inputs, then press the new key/button
or move a stick/trigger. Esc cancels capture. Right-click clears a binding.
Save, revert and per-player reset are available, with a live logical input
check. Each player can disable the keyboard or select either gamepad slot.
Assigning an occupied gamepad slot swaps the other player's slot.

Bindings persist atomically in `config/controls.ini` and load on game launch.
Changes apply to US menu controls, both local selectors and JP battle inputs.
Press **F4** during play to remap immediately without restarting.
Connection order determines gamepad slots; unplugging one seat preserves the
other. Both programs load the bundled `resource/controllerdb.txt` before opening pads.

Local VS opens shared local VS with JP combat and all 24 original grid
choices. Training Mode uses the same roster with the native JP practice engine.
VS Mode opens an ImGui connection window over JP selection, with experimental
code/link hosting and joining with automatic address discovery and native
AutoPunch-compatible hole punching. No fallback relay is configured.
See [online VS](Online-VS.md).

## Build and checks

`build-launcher.ps1` builds the launcher and copies SDL3.dll, controllerdb and
licenses into `app/`. `build.ps1` also builds the US host, JP combat module and
selection art. Close running executables before replacing them. Saved settings
and bindings are preserved by packaging.

`seed_launcher_tests` checks hashing, unsupported-file rejection, settings
replacement, Unicode paths, Windows argument quoting, child launch and exit
reporting. `seed_controls_tests` covers independent remapping, removed old
bindings, alternate axes/triggers, disabled sources, Unicode/atomic persistence,
invalid configs and two SDL virtual gamepads. The actual UI was used to change
P2 A to Q and save it independently of P1. The remapping table was visually
checked at the normal launcher size after fixing unstable column sizing.

## In-game controls (F4)

F4 opens the same per-player binding choices over the current game. Offline
sessions automatically pause simulation and audio; an active online session
continues advancing while the editor is open. Local gameplay inputs are held
neutral during editing and require release before resuming, preventing a
captured key from also attacking or selecting.

Click **Save and close**, F4 or Esc to save and apply edits immediately. Esc
during capture cancels that capture first. **Discard changes** closes without
saving. The launcher reloads in-game edits when its game process exits. F4 is
reserved for this overlay. Production online tests kept F4 open for 50 frames
while two real UDP/Gekko peers continued and converged, including under a
Clumsy delay/loss profile. The user also confirmed a successful rollback
match between North Carolina and Georgia (NC vs GA). Project status lists the
JP combat roster and this rollback test under **Ready**.

The main-menu label is now **LOCAL VS**, drawn with original glyphs copied from
NORMAL and VS menu art plus the SELECT footer. Palette indices and highlight colors are
preserved; the ROM file is not modified.

## Portable package

The portable build keeps bundled game paths relative to the launcher and includes
its Windows runtime DLLs. Extract the whole ZIP and run GundamSeedLauncher.exe.
See [portable packaging and checks](Portable.md).


## Player names and title statistics

Set **Player name** in the online window before hosting or joining. Names are
saved beside the game in `player-name.txt`, support UTF-8 and are bounded to 48
bytes. Both peers receive the same player order. During online combat the
window title displays both names, the configured input delay in frames, the
most recent rollback length in frames and measured round-trip ping in ms.
The title refreshes four times per second. This is display metadata and does
not enter the deterministic game state. Both players must use this build.

## First-launch local asset generation

Release downloads do not include cartridges, BIOS or the generated selector
pack. Select and verify your own three inputs under Game files. Launch game is
available without a pre-existing art cache and generates that cache locally
before starting the US menu. A source build script is not required for setup.

Training uses an English pause menu with the US ROM font. F5 opens the frame meter, F6 toggles red hitboxes/green hurtboxes, and F2/F3 save/reload a practice situation. F7 saves a clean PNG on a solid #101C2B background, retaining enabled F5/F6 overlays. Open the snapshots folder from Play. These tools are offline Training only; see [Training tools](Training-Tools.md).

Game window size is on **Play → Battle display**, beside the outline controls.
**Join Discord** opens the project's server invite from Play. First launch keeps
the launcher visible while `first-launch.log` records local asset preparation;
the native game starts automatically in a fresh process after preparation
succeeds. A failed preparation retains its Windows exit code and the log link.
The original issue #1 log alone does not establish a crash cause. A cold-cache
test took 134 seconds on the test machine; the warm launch took 1.4 seconds.
The `NOT_STATIC` fallback diagnostic remains available and is not treated as a
fatal error. `tools/verify_first_launch.py` verifies cold/warm native startup
and that warm launches reuse generated caches.

## Movelist and turbo (0.1.7)

F1 opens the [JP movelist](Movelist.md) using Kenney D-pad and game-button
glyphs. Like F4, it pauses offline simulation/audio and holds local input
neutral while online matches continue. F1 and F4 are reserved during remapping.
Turbo is exclusively keyboard Tab; old controller fast-forward settings are
ignored, so RT/R2 remains available as an ordinary remappable input.

Settings and scripts live under config/; logs under Logs/; icons,
controllerdb and licenses under resource/. Relative game paths still resolve
from the portable root. Old root settings migrate at startup; conflicting
versions are retained in config/migration-backups/.
