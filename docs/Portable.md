# Portable Windows build

The personal portable package is in the workspace's outputs folder as
`GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-v0.1.0.zip`.
Extract the whole ZIP and run `GundamSeedLauncher.exe`; choose **Launch game**.
The package includes the user's verified US/JP cartridges and BIOS, native game
and JP combat module, selector assets, controller mappings, saves and release
x64 C++ runtime DLLs. Runtime licenses are included beside the game.

Online VS uses shareable SEED1 codes and seedpc invitation links, automatic
address discovery and native AutoPunch-compatible hole punching. The optional
Windows invitation handler can be enabled in the online window; enable it again
if this portable folder moves. There is no configured fallback relay.

The launcher resolves bundled paths relative to its own directory and saves
those paths relatively, so moving the folder or changing the working directory
does not require selecting the files again. External file selections continue
to use absolute paths. All game settings and saves remain in the portable folder.

Validation under `validation/portable/` covers extraction checksums, a renamed
folder with spaces, launch from an unrelated working directory, actual loading
of local SDL3/C++ DLLs, default-BIOS startup, US menu routing and JP Training
selection, pause/resume and reset. These checks ran on the current Windows PC.

`tools/build_portable.py` builds a package from explicit verified inputs and
refuses to replace an existing output folder or ZIP. ZIP integrity and a SHA-256
sidecar are generated. The C++ runtime DLLs are copied unmodified from the
Visual Studio release x64 redistributable directory, following Microsoft's
[application-local deployment documentation](https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp?view=msvc-170).

The current Online package adds saved player names and a live match title:
`Kira vs Athrun | Delay 2f | Rollback 1f | Ping 13ms`. Names, input delay,
last rollback length and round-trip ping are displayed during online combat.
The packaged game passed a named two-peer match with 240 frames and identical
machine checksums. Production SDL windows also verified the actual title while
rollback and F4 remapping continued. `validation/portable/online-names.json`
records the renamed Unicode folder, fresh/existing invitation and bundled DLL
checks for this ZIP.

Release packages record their version and source commit in the portable manifest
and START HERE.txt. The v0.1.0 release also includes the named source/address
cleanup and quiet successful-match teardown fix described in its release notes.
