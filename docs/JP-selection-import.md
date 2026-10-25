# Full Japanese character select and roster

Requested on October 25, 2026: retain the US game as the project base and use the
Japanese SEED Destiny character-select screen and complete mobile suit roster.
This replaces the earlier single-character donor scope. The supplied comparison
screenshot is the visual reference for the two portrait grids and central matchup.
Both local players must select independently in one game instance.

## Verified import pipeline

`tools/import_jp_assets.py` requires the exact B42J cartridge SHA-256 recorded in
`config/content.json`. It resolves the resource archive using JP routine
`0x08000CAC`: a group-relative 32-bit offset at root `0x08787970` locates the group
index, then a group-relative 16-bit offset locates the requested resource.

Each group's index includes an end sentinel. Aligned indices can include padding
after that sentinel. The importer stops at the sentinel. Raw files preserve their
physical extent, including alignment padding; their logical structures still need
to be interpreted by the renderer or data consumer.

| Group | Resources | Observed consumers |
|---|---:|---|
| 3 | 33 | Character-select background tiles, maps and palettes |
| 4 | 137 | Shared UI and portrait-related graphics/palettes |
| 17 | 40 | First portrait graphics/palette bank |
| 18 | 72 | Additional portrait graphics/palettes and other resources |
| 19 | 11 | Selection-screen object graphics and related data |

Total: **293 resources**, including **165** compressed `Le` blocks with
**340,820 decompressed bytes**. These are selection-related archive groups,
not a complete extraction of every fighter's moves, hitboxes or sound data.

The comparison test executes the cartridge's ARM decoder at `0x08084C18` on
all 165 compressed blocks. Both the extracted bytes and consumed source lengths
match the original routine for every block. Run `verify-jp-assets.ps1` to repeat it.

## Selection and roster evidence

| JP address | Observed role |
|---|---|
| `0x080333B4` | Character-select state dispatcher |
| `0x080335F0` | Initializes selection state, eligibility masks and per-side fields |
| `0x08033C3C` | Builds the selection screen and loads both sides' assets |
| `0x0803A0F8` | Staged shared selection graphics preparation |
| `0x0803A3B8` | Constructs display objects and positions |
| `0x08081B08` | Portrait graphics/palette path using groups 17/18 |
| `0x08081C90` | Portrait path using group 4 |
| `0x030075AC` | Pointer to the current selection work structure |
| Work offsets 79/80 | The two sides' selected portrait indices |
| `0x080800F0` | Builds an eligibility mask over indices 0 through 27 |
| `0x08080864` | Checks availability/unlocks for a supplied index |

Selection initialization visits indices 0 through 24, then separately checks
indices 23, 25 and 26. This demonstrates additional variant/unlock handling;
it does not establish a simple count of independently playable mobile suits.
The mapping from those indices to mech names is not yet complete.

JP battle records store fighter IDs in five bits. US records use four bits and
have a different stride and field layout. The expanded native roster must retain
the donor ID range and adapt fighter data, behavior and assets to the US engine.
New IDs must not be masked with the US `0x0F` mask.

Decoded sprite tiles require matching sprite metadata because display objects
are assembled from multiple parts. Rendering that composition and connecting
independent local controls remain unimplemented.
