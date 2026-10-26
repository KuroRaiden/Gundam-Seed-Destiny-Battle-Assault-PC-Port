# Named address reference

Generated from `analysis/address-map.json`. US and JP layouts are independent.
Names describe verified use; unidentified original option meanings are stated explicitly.
Hardware ranges and private test sentinels are distinguished from original game symbols.
The C++ API is in `src/game_addresses.h`; Python tools import `tools/game_addresses.py`.

## COMMON

| Symbol | Value | Meaning | Evidence |
| --- | --- | --- | --- |
| `common::kEwramBegin` | `0x02000000` | External work RAM start. | GBA memory map or explicitly allocated test sentinel |
| `common::kEwramEnd` | `0x02040000` | Exclusive external work RAM end. | GBA memory map or explicitly allocated test sentinel |
| `common::kIwramBegin` | `0x03000000` | Internal work RAM start. | GBA memory map or explicitly allocated test sentinel |
| `common::kIwramEnd` | `0x03008000` | Exclusive internal work RAM end. | GBA memory map or explicitly allocated test sentinel |
| `common::kIoBegin` | `0x04000000` | Memory-mapped hardware registers. | GBA memory map or explicitly allocated test sentinel |
| `common::kIoEnd` | `0x04000400` | Exclusive register window end. | GBA memory map or explicitly allocated test sentinel |
| `common::kPaletteBegin` | `0x05000000` | Palette RAM. | GBA memory map or explicitly allocated test sentinel |
| `common::kPaletteEnd` | `0x05000400` | Exclusive palette RAM end. | GBA memory map or explicitly allocated test sentinel |
| `common::kVramBegin` | `0x06000000` | Video RAM. | GBA memory map or explicitly allocated test sentinel |
| `common::kVramEnd` | `0x06018000` | Exclusive video RAM end. | GBA memory map or explicitly allocated test sentinel |
| `common::kObjectTilesBegin` | `0x06010000` | Object tile storage used by the US menu compositor. | GBA memory map or explicitly allocated test sentinel |
| `common::kOamBegin` | `0x07000000` | Object attribute memory. | GBA memory map or explicitly allocated test sentinel |
| `common::kOamEnd` | `0x07000400` | Exclusive object attribute memory end. | GBA memory map or explicitly allocated test sentinel |
| `common::kRomBegin` | `0x08000000` | Cartridge ROM mapping and header entry. | GBA memory map or explicitly allocated test sentinel |
| `common::kRomCodeEntry` | `0x080000C0` | Executable cartridge entry after the GBA header. | GBA memory map or explicitly allocated test sentinel |
| `common::kSerialMulti0` | `0x04000120` | First multiplayer serial word; aliases SIODATA32. | GBA memory map or explicitly allocated test sentinel |
| `common::kSerialControl` | `0x04000128` | SIOCNT serial control register. | GBA memory map or explicitly allocated test sentinel |
| `common::kSerialSend` | `0x0400012A` | Multiplayer serial transmit register. | GBA memory map or explicitly allocated test sentinel |
| `common::kKeyInput` | `0x04000130` | Active-low keypad input register. | GBA memory map or explicitly allocated test sentinel |
| `common::kKeyInterruptControl` | `0x04000132` | Keypad interrupt control register. | GBA memory map or explicitly allocated test sentinel |
| `common::kSerialModeControl` | `0x04000134` | RCNT serial/GPIO mode register. | GBA memory map or explicitly allocated test sentinel |
| `common::kOracleStackTop` | `0x03007E00` | Scratch stack used by interpreter comparison tests. | GBA memory map or explicitly allocated test sentinel |
| `common::kHookReturnMarker` | `0x03007F00` | Private return sentinel for short guest calls; not an original game function. | GBA memory map or explicitly allocated test sentinel |
| `common::kOracleReturnMarker` | `0x03007FF0` | Return sentinel used by isolated interpreter oracles. | GBA memory map or explicitly allocated test sentinel |
| `common::kOracleThumbReturnMarker` | `0x03007FF1` | Thumb return sentinel; bit 0 requests Thumb mode. | GBA memory map or explicitly allocated test sentinel |
| `common::kOracleCopyDestination` | `0x02001000` | Scratch destination used only by BIOS copy tests. | GBA memory map or explicitly allocated test sentinel |
| `common::kBiosCpuFastSet` | `0x00000BC4` | Entry of the verified BIOS CpuFastSet implementation exercised by its oracle. | GBA memory map or explicitly allocated test sentinel |

## US

| Symbol | Value | Meaning | Evidence |
| --- | --- | --- | --- |
| `us::kMainLoop` | `0x080003A0` | Main loop. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kGameState` | `0x03000F48` | Game state. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kSubstate` | `0x030000B0` | Substate. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kGameMode` | `0x030075D0` | Game mode. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kLinkStatus` | `0x03007610` | Link status. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kBattleUpdate` | `0x0800242C` | Battle update. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kBattleContext` | `0x03000500` | Battle context. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kCharacterSelectDispatch` | `0x0802A660` | Character select dispatch. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kCharacterSelectTable` | `0x080AEB2C` | Character select table. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kCharacterSelectDualCommandReader` | `0x080338C0` | Character select dual command reader. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `us::kSelectedFighter1` | `0x03007620` | Native fighter ID for player 1; US IDs and JP IDs have different layouts. | Port hook/capture usage; see source and validation reports |
| `us::kSelectedFighter2` | `0x03007621` | Native fighter ID for player 2; US IDs and JP IDs have different layouts. | Port hook/capture usage; see source and validation reports |
| `us::kReceivedValuePointer1` | `0x03007600` | Pointer to link value for seat 1; command/input meaning depends on game state. | Port hook/capture usage; see source and validation reports |
| `us::kReceivedValuePointer2` | `0x03007604` | Pointer to link value for seat 2; command/input meaning depends on game state. | Port hook/capture usage; see source and validation reports |
| `us::kCharacterSelectSub04` | `0x0802A96C` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub05` | `0x0802A9EC` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub06` | `0x0802AA60` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub07` | `0x0802AC34` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub08` | `0x0802AD8C` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub09` | `0x0802B1F4` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub10` | `0x0802B384` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub11` | `0x0802B278` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub12` | `0x0802B4B4` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub13` | `0x0802BD18` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub14` | `0x0802C648` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub15` | `0x0802D704` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub16` | `0x0802E14C` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub17` | `0x0802E7B8` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub18` | `0x0802ECB0` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub19` | `0x0802FC30` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub20` | `0x08030624` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub21` | `0x08030AE8` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub22` | `0x0803123C` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub23` | `0x080319CC` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub24` | `0x080323FC` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub25` | `0x08032DA4` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub26` | `0x080334E8` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub27` | `0x080338C0` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub28` | `0x080344E0` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub29` | `0x08034B5C` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub30` | `0x08035150` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kCharacterSelectSub31` | `0x080358D0` | Original character-select substate dispatcher entry; behavior beyond the numbered state is not assigned a speculative name. | Existing IDA address export |
| `us::kPlayer1InputUpdate` | `0x08000860` | Player 1 input update. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1Held` | `0x03000F44` | Player 1 held. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1Previous` | `0x030004D0` | Player 1 previous. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1Pressed` | `0x0300001C` | Player 1 pressed. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1Released` | `0x03000600` | Player 1 released. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1Repeat` | `0x03000F6C` | Player 1 repeat. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1RecentRepress` | `0x030004C8` | Player 1 recent repress. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1RepeatTicks` | `0x030000A0` | Player 1 repeat ticks. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer1History` | `0x03000070` | Player 1 history. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2InputUpdate` | `0x08000934` | Player 2 input update. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2Held` | `0x03000F74` | Player 2 held. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2Previous` | `0x0300060C` | Player 2 previous. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2Pressed` | `0x030004EC` | Player 2 pressed. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2Released` | `0x03000F68` | Player 2 released. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2Repeat` | `0x03000098` | Player 2 repeat. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2RecentRepress` | `0x030004CC` | Player 2 recent repress. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2RepeatTicks` | `0x030004E0` | Player 2 repeat ticks. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kPlayer2History` | `0x03000F80` | Player 2 history. | IDA disassembly plus 8,192 original input-routine oracle calls |
| `us::kNextScene` | `0x03000090` | Requested next scene used during the original fade/cleanup transition. | Port hook/capture usage; see source and validation reports |
| `us::kCharacterSelectTick` | `0x0802D704` | Hook replacing the original character-select control update. | Port hook/capture usage; see source and validation reports |
| `us::kCharacterSelectBegin` | `0x0802A684` | Original selection entry wrapper used to rebuild independent seat state. | Port hook/capture usage; see source and validation reports |
| `us::kMapFighterButtons` | `0x08004574` | Original raw-button-to-fighter action mapper. | Port hook/capture usage; see source and validation reports |
| `us::kPlaySelectionSound` | `0x08001F44` | Guest sound helper called with SFX 14 when a cursor changes. | Port hook/capture usage; see source and validation reports |
| `us::kSelectionSoundGate` | `0x03007644` | Cursor SFX is played only when zero; full setting semantics remain unidentified. | Port hook/capture usage; see source and validation reports |
| `us::kMenuCursorX` | `0x03000533` | Main-menu column; alias of context + 51 in the menu state. | Port hook/capture usage; see source and validation reports |
| `us::kMenuCursorY` | `0x03000534` | Main-menu row; alias of context + 52 in the menu state. | Port hook/capture usage; see source and validation reports |
| `us::kMenuReadyPhase` | `0x03000536` | Main-menu initialization phase; value 3 permits routing/input. | Port hook/capture usage; see source and validation reports |
| `us::kSelectionControlPhase` | `0x03000556` | Selection control phase; context + 86. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer1BattleHeld` | `0x0300054C` | Fighter 1 raw held mask at context + 76. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer1BattleRepress` | `0x03000550` | Fighter 1 repeated-press mask at context + 80. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer1BattlePressed` | `0x03000554` | Fighter 1 pressed mask at context + 84. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer2BattleHeld` | `0x030005B0` | Fighter 2 raw held mask at context + 176. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer2BattleRepress` | `0x030005B4` | Fighter 2 repeated-press mask at context + 180. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer2BattlePressed` | `0x030005B8` | Fighter 2 pressed mask at context + 184. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer1Health` | `0x0300053E` | Fighter 1 health; context + 62 in the battle state. | Port hook/capture usage; see source and validation reports |
| `us::kPlayer2Health` | `0x030005A2` | Fighter 2 health; context + 162 in the battle state. | Port hook/capture usage; see source and validation reports |
| `us::kStateMainMenu` | `4` | US main-menu scene. | Port hook/capture usage; see source and validation reports |
| `us::kStateCharacterSelect` | `11` | US selector dispatcher, replaced by the JP compositor. | Port hook/capture usage; see source and validation reports |
| `us::kModePresetVersus` | `6` | US non-link preset battle rules used by the hook. | Port hook/capture usage; see source and validation reports |
| `us::kFighterStride` | `100` | Stride between native US fighter records. | Port hook/capture usage; see source and validation reports |

## JP

| Symbol | Value | Meaning | Evidence |
| --- | --- | --- | --- |
| `jp::kBattleInit` | `0x08002DC0` | Battle init. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `jp::kCharacterSelectDispatch` | `0x080333B4` | Character select dispatch. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `jp::kCharacterSelectWorkPointer` | `0x030075AC` | Character select work pointer. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `jp::kResourceArchive` | `0x08787970` | Resource archive. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `jp::kResourceAccessor` | `0x08000CAC` | Resource accessor. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `jp::kLeDecoder` | `0x08084C18` | Le decoder. | Existing analysis/verified-addresses.json and docs/IDA-findings.md |
| `jp::kSelectedFighter1` | `0x030075F0` | Native fighter ID for player 1; US IDs and JP IDs have different layouts. | Port hook/capture usage; see source and validation reports |
| `jp::kSelectedFighter2` | `0x030075F1` | Native fighter ID for player 2; US IDs and JP IDs have different layouts. | Port hook/capture usage; see source and validation reports |
| `jp::kGameState` | `0x03000CBC` | JP scene dispatcher index; 4 = versus, 8 = training, 21 = character selection. | Port hook/capture usage; see source and validation reports |
| `jp::kSubstate` | `0x03000DDC` | JP scene substate; used by native training pause/resume. | Port hook/capture usage; see source and validation reports |
| `jp::kBattleContext` | `0x03000CD0` | JP battle context; its fighter records have a different stride from the US layout. | Port hook/capture usage; see source and validation reports |
| `jp::kStartupResetField` | `0x03000C34` | Reset to zero before direct scene entry; full original field semantics remain unidentified. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer1InputState` | `0x03000C40` | First native JP input-state record. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer2InputState` | `0x03000C78` | Second native JP input-state record. | Port hook/capture usage; see source and validation reports |
| `jp::kUpdateInputState` | `0x080025C0` | Native JP input-state update called with record pointer and active-high keys. | Port hook/capture usage; see source and validation reports |
| `jp::kInputDispatch` | `0x08002654` | JP input hook responsible for the two local streams and direct match startup. | Port hook/capture usage; see source and validation reports |
| `jp::kRightControlDispatch` | `0x08003418` | Original right-fighter control branch intercepted for local P2; Training keeps the native dummy. | Port hook/capture usage; see source and validation reports |
| `jp::kApplyFighterInput` | `0x08003C78` | Native helper copying a JP input-state record into a fighter input block. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayerStartupParameterA` | `0x030075F2` | Two per-seat bytes initialized to 4; original option meaning is not yet identified. | Port hook/capture usage; see source and validation reports |
| `jp::kSharedStartupParameterA` | `0x030075F4` | Shared startup byte initialized to zero; original option meaning is not yet identified. | Port hook/capture usage; see source and validation reports |
| `jp::kSharedStartupParameterB` | `0x030075F5` | Second shared startup byte initialized to zero; original option meaning is not yet identified. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayerStartupParameterB` | `0x030075F6` | Two per-seat startup bytes initialized to zero; original option meaning is not yet identified. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayerStatParameters` | `0x03007604` | Three stat parameter bytes per seat, 4-byte stride; initialized to 100 for standard combat. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayerButtonMapping` | `0x03007620` | Per-seat raw A/B/L/R mapping; 0xE1 supplies the identity mapping. | Port hook/capture usage; see source and validation reports |
| `jp::kSceneSetupByte` | `0x03007630` | Native scene setup byte initialized to 1 by both combat and selector capture. | Port hook/capture usage; see source and validation reports |
| `jp::kMatchRuleMode` | `0x03007631` | Native rule selector: direct training uses 2; versus uses 6. | Port hook/capture usage; see source and validation reports |
| `jp::kSelectorTransientState` | `0x0300766C` | 13-byte state range reset during the build-time native selector capture. | Port hook/capture usage; see source and validation reports |
| `jp::kSelectorActorRecords` | `0x03000FA0` | 32-byte selector actor records; capture hides selected actors to obtain static art. | Port hook/capture usage; see source and validation reports |
| `jp::kSpriteBankTable` | `0x08086054` | ROM table used to resolve multipart selector sprite banks and frames. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer1Health` | `0x03000D04` | JP P1 health at battle context + 52. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer2Health` | `0x03000D64` | JP P2 health at battle context + 148. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer1MappedInput` | `0x03000D14` | JP P1 mapped action mask at battle context + 68. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer2MappedInput` | `0x03000D74` | JP P2 mapped action mask at battle context + 164. | Port hook/capture usage; see source and validation reports |
| `jp::kRoundPhase` | `0x03000CEE` | Native round phase at battle context + 30. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer1FighterId` | `0x03000CFE` | Native P1 fighter ID packed at context + 46; use JP mask 0x1F. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayer2FighterId` | `0x03000D5E` | Native P2 fighter ID packed at context + 142; use JP mask 0x1F. | Port hook/capture usage; see source and validation reports |
| `jp::kStateVersus` | `4` | Native JP versus battle. | Port hook/capture usage; see source and validation reports |
| `jp::kStateTraining` | `8` | Native JP training battle. | Port hook/capture usage; see source and validation reports |
| `jp::kStateCharacterSelect` | `21` | Native JP character-selection scene. | Port hook/capture usage; see source and validation reports |
| `jp::kRightFighterInputOffset` | `140` | Offset added to the context pointer passed by the right-control dispatcher. | Port hook/capture usage; see source and validation reports |
| `jp::kFighterStride` | `96` | Stride between native JP fighter records. | Port hook/capture usage; see source and validation reports |
| `jp::kPlayerParameterStride` | `4` | Stride between JP per-player stat parameter arrays. | Port hook/capture usage; see source and validation reports |
| `jp::kSelectorActorStride` | `32` | Size of a native selector actor record. | Port hook/capture usage; see source and validation reports |
| `jp::kRomEnd` | `0x08800000` | Exclusive end of the supplied 8 MiB JP cartridge; also bounds address discovery in the shared analysis tool. | Verified cartridge size and ROM base |
