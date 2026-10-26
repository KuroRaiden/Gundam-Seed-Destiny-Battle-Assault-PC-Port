# Gundam SEED: Battle Assault — native PC port work

This is an **experimental PC port**, based on the verified US BGNE cartridge.
Local VS now opens shared local VS: both players select independently,
fight in one game instance, and return to selection after a match. Local VS uses
the Japanese Destiny selection screen and combat engine behind the US menus.

Current implemented work:

- A native Dear ImGui launcher with file verification, saved paths, per-player
  keyboard/gamepad remapping, input checks, and launch/error reporting.
- US cartridge verification, native source generation, and an SDL3 host.
- SDL3 3.4.18 for the game and launcher, with a shared `controllerdb.txt`.
- Local VS, with independent keyboard/gamepad inputs,
  two selection cursors, ready/cancel, JP battles, and rematch selection.
- Training Mode with the JP roster and native JP practice engine. One player
  chooses their fighter and practice opponent on the JP grid.
- VS Mode opens an in-game ImGui lobby with direct UDP hosting, copyable
  invitations, independent JP selection and GekkoNet rollback combat.
- Saved online player names and live title-bar input delay, rollback and ping.
- Correct ARM32 IDA databases for the US and Japanese ROMs.
- Verified main-loop, two-player input, character-select, and battle addresses.
- A deterministic two-player input model checked against 8,192 calls to the
  original cartridge's input routines.
- A verified importer for 389 JP selection-related resources; all 223 compressed
  blocks match the original cartridge decoder.
- The original JP grid, background, mech/pilot/name previews and independent
  local cursors. All 24 original JP grid choices are playable in both seats.

Online VS creates a shareable **SEED1 code** and `seedpc://` invitation link.
The host chooses **Create code**; the opponent pastes the code or link in **Join**.
Address discovery, port selection and AutoPunch-compatible hole punching run
automatically. Click **Enable invitation links on this PC** in the online window
to let Windows open links with this portable copy. Connections use direct UDP;
networks that block direct connections may fail. No fallback relay is configured.
See [networking status](docs/Online-VS.md).

See [the IDA findings](docs/IDA-findings.md) and the address map under `analysis/`.
The Japanese donor is SEED Destiny, which has different fighter layouts from
the US game. The requested content is now its full roster and selection screen;
see [the import findings](docs/JP-selection-import.md).

## Launcher

Run `app/GundamSeedLauncher.exe`. In **Game files**, select the US cartridge,
the Japanese donor cartridge, and an extracted GBA BIOS image. Supported file
revisions are verified by size and hash. Both cartridges and the BIOS are required
to launch. The Japanese file supplies the selector art and native local combat.
The generated selection pack must stay in `app/assets/` beside the host.
Paths and the window scale are saved beside the executable. ROMs are read without
being modified.

**Play** launches the experimental host from the same folder and restores the
launcher when it closes. A failed run shows its exit code and offers the game
log. **Controllers** remaps keys, gamepad buttons, sticks and triggers separately
for P1 and P2. Save bindings before launch; profiles persist in `app/controls.ini`.
Choose **Local VS** in the game's main menu for local VS. The main menu's
**VS Mode** opens the ImGui online lobby using JP selection; the old US cable
Connecting screen is bypassed. **Training Mode** uses the JP roster and practice
engine. Host or paste an invitation in VS Mode for experimental direct online
play. Online uses your Player 1 bindings on both machines; F4 remapping keeps
the match running with your inputs neutral. See [online setup and validation](docs/Online-VS.md)
and [local VS controls and validation](docs/Local-VS.md).

See [launcher details and validation](docs/launcher.md). Close the launcher before
building so Windows can replace its executable.

## Workspace build

This project uses the dependencies already checked out under `../../work/deps`
and private generated cartridge/BIOS code under `../../work`. Versions are
recorded in `dependencies.lock.json`. Visual Studio 2022 C++ and CMake are used.
It is currently tied to this workspace rather than packaged as a standalone
source distribution.

```powershell
.\build-launcher.ps1
.\build.ps1 -Rom 'path/to/US.gba' -Bios 'path/to/gba_bios.bin'
.\verify-input.ps1 -Rom 'path/to/US.gba'
.\verify-jp-assets.ps1 -JpRom 'path/to/JP.gba'
```

`build-launcher.ps1` builds the launcher without generating cartridge or BIOS
code. It still uses the pinned dependency checkouts in this workspace.
SDL3 is checked out under `../../work/deps/SDL3` at the revision in
`dependencies.lock.json`. The bundled community mappings come from
[SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB), with its
license preserved. Both executables load `app/controllerdb.txt` at startup.
`build.ps1` also accepts `-JpRom` to import the donor resources and builds both
the host and launcher, captures the original donor screen, and builds the JP
art pack, copying the runtime files into `app/`. The baseline host also remains
under `../../work/build/seed/Release/GundamSeedRecomp.exe`. `verify-input.ps1` builds
and runs the cartridge comparison for the input module. Generated ROM code,
BIOS bytes, and the original ROMs remain private workspace inputs.

The checked-out GBARecomp source includes the MSVC portability fix in
`msvc-countl-zero.patch` and IRQ return-stack cleanup in
`runtime-irq-return-stack.patch`, plus the SDL3 host selection support in
`runtime-sdl3-host.patch`, idle debugger shutdown in `runtime-debug-stop.patch`,
and the native frame/snapshot driver in `runtime-native-frame-driver.patch`.
`build.ps1` applies missing patches without resetting
the dependency checkout. The IRQ patch fixes the reproduced long-run call-stack overflow.
Some cartridge paths still use the framework's interpreter bridge for missing
static coverage; a fully static build remains unfinished.
The framework uses the PolyForm Noncommercial license; the
other selected dependencies retain their upstream licenses. They have not all
been integrated into the executable.
See [SDL3 migration and validation](docs/SDL3.md).

## IDA workflow

`tools/ida_prepare_gba.py` creates a correctly mapped database from a **fresh
workspace ROM copy**, using the Python environment configured for IDA's
`idapro` module. `tools/ida_research.py` applies explicit analysis jobs and exports
disassembly, pseudocode, tables and references. Neither script patches cartridge
bytes. Close any MCP session owning a database before running a batch script
against the same database.

The saved databases in `analysis/` contain the current research annotations.
The raw evidence exports include decompiler output; inferred prototypes should
be checked against assembly before writing replacements.

See [JP combat and validation](docs/JP-combat.md) for the one-process handoff,
full grid roster smoke checks and current limits.

## Readable source and address reference

See [the source guide](docs/Source-Guide.md) for the launcher, menu hooks,
JP combat, rollback and transport entry points, and [the named address map](docs/Address-Map.md)
for the US/JP constants and evidence. `analysis/address-map.json` generates
both the C++ and Python symbols. `python tools/check_source_readability.py`
checks that these files agree and that raw guest addresses stay out of the
hand-written source. This cleanup covers the port and its tools, not semantic
decompilation of the private generated cartridge instructions.

## First portable release

`v0.1.0` packages the current Windows x64 build, Local VS, JP Training and
online code/link play with rollback, saved player names and live title stats.
The repository and its release remain private. Release downloads contain runtime files and licenses only. Players supply
their own US/JP cartridges and GBA BIOS; selector art is generated locally.
The user's two-PC online smoke test and automated checks are recorded separately
under `validation/`.

## Distribution correction (v0.1.1)

The original v0.1.0 personal bundle was uploaded incorrectly and withdrawn.
Release packages must never include ROMs, BIOS, generated art, saves or user
profiles. The runtime-only packager and pre-upload audit enforce this rule.
See [portable setup](docs/Portable.md). Original local game/BIOS files are
retained on the developer's PC; they are not release inputs.
