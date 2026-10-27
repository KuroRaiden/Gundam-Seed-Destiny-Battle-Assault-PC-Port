# Stage select

After both fighters are confirmed in Local VS, Online VS or Training, stage
select opens in the same game window. Player 1 chooses locally and in Training;
the host chooses online while the guest sees the current preview. Left/right
cycles the 25 normal native JP stages and Random. A confirms. B returns to
character select; online, either player can request that return before the host
confirms. Release the character-confirm input before confirming the stage.

The last cursor is retained for the next rematch, including Random. Random
resolves once on the host before the acknowledged startup packet. Repeated
online matches retain their room, invitation, names and palettes.

Stage labels currently read Stage 01 through Stage 25 (native IDs 0 through 24).
Some IDs are native variations that share background art. Special mode-only
stage ID 25 is excluded from normal versus/Training selection.

## Native integration

IDA traced `jp::kSelectedStage` into the VS and Training initializers, then the
stage resource loader through `jp::kBattleContext + jp::kBattleStageOffset`.
`JpCombatRequest` API 8 carries the explicit stage, validated before DLL entry.
Snapshot provider `seed.jp-match` schema 4 includes that immutable request field;
snapshots from another stage fail preflight before changing the simulation.

Room protocol 9 includes the stage in its start offer/ack and combat control
configuration. A host-owned selection revision rejects ready packets from
before a return to character select. Older online builds are incompatible.

The first launch generates `assets/destiny-stages.bin` locally from the verified
Japanese ROM. The native renderer captures only the stage background planes for
previews; gameplay retains the original rendering and stage behavior. No stage
preview pack is shipped. This preparation runs once per extracted portable.

## Validation

- `verify_jp_stages.py`: all 25 normal stages initialized, movement/attacks
  advanced, and the requested native stage verified in both VS and Training.
- `seed_stage_selection_tests`: all stage choices, Random, wraparound,
  character-confirm release guard, Back and guest input ownership.
- `seed_stage_room_tests`: shared previews, host/guest Back, four stage/rematch
  agreements with one invitation, and rejected invalid/wrong-stage/old-protocol
  combat-start retries.
- Updated menu integration checks cover real Local VS KOs and stage changes
  on rematch, Training pause/reset, and repeated production online combat.
- A native Gekko stress run exercises snapshot restoration and replay on stage 24.

These are local-machine automated checks. The new stage-selection protocol has
not yet been tested between two PCs over the Internet.
