# Build and research notes

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
See [SDL3 migration and validation](SDL3.md).

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

See [JP combat and validation](JP-combat.md) for the one-process handoff,
full grid roster smoke checks and current limits.
