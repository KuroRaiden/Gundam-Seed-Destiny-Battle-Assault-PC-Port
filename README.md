# Gundam SEED: Battle Assault — native PC port work

This is an **incomplete development project**, based on the verified US BGNE
cartridge. The native executable builds and reaches the intro, but longer runs
still fail in the framework's generated call-return handling. It is not a
playable release.

Current implemented work:

- US cartridge verification, native source generation, and an SDL2 host.
- Correct ARM32 IDA databases for the US and Japanese ROMs.
- Verified main-loop, two-player input, character-select, and battle addresses.
- A deterministic two-player input model checked against 8,192 calls to the
  original cartridge's input routines.
- A verified importer for 293 JP selection-related resources; all 165 compressed
  blocks match the original cartridge decoder.

The game does not yet have integrated local VS, online rollback, join links,
NAT traversal, or the Japanese screen/roster in gameplay. The five approved fallback
relay hostnames are recorded in `config/network.json`; their protocol and port
are pending.

See [the IDA findings](docs/IDA-findings.md) and the address map under `analysis/`.
The Japanese donor is SEED Destiny, which has different fighter layouts from
the US game. The requested content is now its full roster and selection screen;
see [the import findings](docs/JP-selection-import.md).

## Workspace build

This project uses the dependencies already checked out under `../../work/deps`
and private generated cartridge/BIOS code under `../../work`. Versions are
recorded in `dependencies.lock.json`. Visual Studio 2022 C++ and CMake are used.
It is currently tied to this workspace rather than packaged as a standalone
source distribution.

```powershell
.\build.ps1 -Rom 'path/to/US.gba' -Bios 'path/to/gba_bios.bin'
.\verify-input.ps1 -Rom 'path/to/US.gba'
.\verify-jp-assets.ps1 -JpRom 'path/to/JP.gba'
```

`build.ps1` also accepts `-JpRom` to import the donor resources and produces the baseline host under
`../../work/build/seed/Release/GundamSeedRecomp.exe`. `verify-input.ps1` builds
and runs the cartridge comparison for the input module. Generated ROM code,
BIOS bytes, and the original ROMs remain private workspace inputs.

The checked-out GBARecomp source includes the one-line MSVC portability fix
recorded in `msvc-countl-zero.patch`; apply that patch to a fresh copy of its
pinned revision. The framework uses the PolyForm Noncommercial license; the
other selected dependencies retain their upstream licenses. They have not all
been integrated into the executable.

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
