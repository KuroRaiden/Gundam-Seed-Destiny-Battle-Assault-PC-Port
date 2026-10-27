# Gundam SEED Destiny / Battle Assault PC Recomp

A Game Boy Advance recomp project bringing **single-screen Local VS** to PC.
The goal is the shared-screen multiplayer feel of Game Boy / Game Boy Color-era
games and the SNES Super Game Boy.

The US **Mobile Suit Gundam SEED: Battle Assault** menus are the base. The US
ROM's **Password Mode is replaced by Local VS**, where both players select their
mobile suits and fight in one game window. Japanese **SEED Destiny roster,
character select and battle-system injection** supplies the expanded combat.

## Features

- **Local VS:** two players, two controllers/keyboards, one screen and one game instance.
- **Black sprite outline:** a simple one-pixel black border around battle fighters. Toggle **Black sprite outlines** and adjust **Outline opacity** (0-100%) on the launcher's **Play** page; saved preferences affect only your display online.
- **Stage select:** 25 native JP stages plus Random, with previews in Local VS, Online VS and Training.
- **Japanese roster and combat:** JP mobile suits, alternate colors and hidden variants behind the US menus.
- **Launcher palette editor:** edit MS colors with a live preview; custom palettes also sync online.
- **Online VS:** rollback, automatic NAT hole punching, join codes/links and unlimited rematches in the same room.
- **Training:** JP practice mode with the US-font English pause menu, **F5 frame meter**, **F6 hitbox/hurtbox viewer**, and **F2 save / F3 load** for practice situations.
- **Launcher updates:** version display and GitHub release checks, with verified in-place updates that preserve player settings and saves.
- **Controls:** launcher remapping and **F4** in-game remapping. **Start** cycles colors on character select.

The main menu's **VS Mode** opens online play. **Local VS** occupies the former
Password entry. Training uses the Japanese roster and battle system too.

## Play

Download a [portable Windows build](https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/latest),
extract it, and run `GundamSeedLauncher.exe`. Published releases may lag the
features on the main branch; the current portable layout is described below.

Put your own supported US ROM, Japanese ROM and extracted GBA BIOS in the
portable's **gamefiles** folder. `Game_and_Bios_Here.file` lists the default
filenames. The launcher prefills relative paths there; **Browse** supports other
names and locations. Verify the files before launching. ROMs and BIOS are not
included in downloads.

## Tools and libraries

| Tool | Use | License |
| --- | --- | --- |
| [GBARecomp](https://github.com/mstan/gbarecomp) | Recompilation framework and runtime | PolyForm Noncommercial 1.0.0 |
| [arm-recomp-core](https://github.com/mstan/arm-recomp-core) | ARM recompilation core | MIT |
| [SDL3](https://github.com/libsdl-org/SDL) | Window, audio and controllers | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | Launcher, palette editor and overlays | MIT |
| [GekkoNet](https://github.com/HeatXD/GekkoNet) | Rollback networking | BSD 2-Clause |
| [AutoPunch](https://github.com/delthas/autopunch) | Compatible NAT rendezvous/hole-punching protocol | MIT |
| [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | Bundled controller mappings | zlib |

Research and testing use [IDA Pro](https://hex-rays.com/ida-pro),
[Clumsy](https://github.com/jagt/clumsy), [CMake](https://cmake.org/),
[Visual Studio C++](https://visualstudio.microsoft.com/vs/features/cplusplus/),
[Python](https://www.python.org/) and [Pillow](https://python-pillow.org/).
GBARecomp also incorporates code from [JRickey/gba-recomp](https://github.com/JRickey/gba-recomp)
(MIT OR Apache-2.0) and mGBA-derived BIOS HLE files (MPL-2.0).

## Licenses and status

GBARecomp's PolyForm Noncommercial terms apply to its framework code. Other
components retain their own licenses. Full notices and the modified MPL-covered
source are in [Licenses and attribution](THIRD_PARTY_NOTICES.md). No separate
project-wide license has been selected for original port integration code.
Original game and BIOS rights remain with their respective owners.

This is experimental. Some guest paths still use the framework's interpreter
bridge; a fully static recompile remains unfinished.

[Portable setup](docs/Portable.md) · [Local VS](docs/Local-VS.md) ·
[Online VS](docs/Online-VS.md) · [Stage select](docs/Stage-Select.md) · [Palette editor](docs/MS-colors.md) ·
[Training tools](docs/Training-Tools.md) · [Frame meter](docs/Training-Frame-Meter.md) · [Source guide](docs/Source-Guide.md) ·
[Build notes](docs/Build.md)
