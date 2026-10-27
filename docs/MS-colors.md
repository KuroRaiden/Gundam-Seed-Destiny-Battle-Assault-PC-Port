# MS color selection and launcher editor

On character select, press Start while unready to cycle colors/variants, then
A to confirm. Each local player uses their own Start binding. Training uses
P1 controls for whichever fighter/dummy is currently being chosen. Color
choices survive a return to selection. Start retains its ordinary pause/options
behavior after entering combat.

All 54 native palette/variant combinations are available. The base slot for
native fighter 1 offers six choices: IDs 1, 26 and 27, each with two palettes.
The base slot for native fighter 23 offers four: IDs 23 and 24, each with two
palettes. The other slots offer two colors. Equal-color mirrors are allowed.

In the launcher, open **MS Colors**. Verify the JP ROM and BIOS, then choose
**Prepare previews** once. Select Player 1/2, the MS and original color slot.
Click a swatch and edit its RGB values or open the picker. The live preview
updates immediately. **Save colors** persists all changes; launching also saves
pending changes. **Reset to original** clears the current override; **Revert
saved** restores all saved profiles. Native MS IDs identify the selector's
suits, including hidden variants. Unused-in-idle swatches can affect attacks,
other poses or effects even when they do not change the idle preview.

Profiles stay in `ms-colors.ini` beside the launcher, keyed by player, native
fighter and original palette. Local VS uses the two players' profiles; Training
uses P1 for the fighter and P2 for the dummy. Online always uses each machine's
Player 1 profiles and transmits the selected palette to the opponent. All peers
validate the same palette before combat. This requires updated builds on both
sides. Existing room codes retain their text format.

The preview is rendered by the original native JP combat/PPU. Two encodings of
the 48-entry P1 palette are rendered from the same restored frame; only pixels
that decode to the same swatch in both captures enter the indexed sprite. This
excludes stage/HUD/opponent pixels without guessing RGB equivalences. The
27-suit indexed cache is generated locally from verified user inputs and stored
in `assets/ms-color-previews.bin`. ROMs and BIOS are never modified.

`src/fighter_colors.h` names the base-slot/variant mapping. The two palette
hooks declared in `game-jp-combat.toml` use explicit player choices, including
mirrors. Custom palettes use the game's existing 96-byte EWRAM palette buffers;
there is no new guest memory reservation. Native form and combat effects keep
their original behavior; this is an MS body/detail palette editor, not a global
editor for stage, projectile, HUD or every alternate-form resource.

Combat ABI 7 and room protocol 8 carry 48 RGB5 colors per enabled seat. Disabled
palettes must contain zero payloads. Selection/start/ack/native-start packets
check the palettes; malformed flags, values and lengths are rejected. Snapshot
provider schema 3 binds restores to fighter IDs, native colors and custom RGB
values. The profile reader rejects malformed/duplicate/out-of-range entries
without partially applying them. No player color files are copied into releases.

Validation: all 54 native ROM palettes; Local VS/Training Start-to-combat and
Training pause/resume; saved-profile round trips/reset and malformed input;
27 indexed previews with changes confined to the edited swatch; full custom
body/detail palette checks in both native seats; actual saved-profile online
menu/selection/rollback/return; production UDP checksums matching at 240 frames;
351 stress restores and 2,457 replayed frames with zero desyncs and 14 malformed
snapshots rejected without live-state changes. The launcher layout was captured
and inspected. Further UI automation was stopped when the user pressed Escape.
