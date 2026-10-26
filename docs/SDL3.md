# SDL3 desktop runtime

The launcher and game use native SDL3 **3.4.18**, pinned to
`829a65d769d935c4852f8159e964312c0957260a`. The game, JP module and launcher share SDL3.dll; none
uses SDL2 or sdl2-compat. Dear ImGui uses its SDL3 platform and SDLRenderer3
backends. The game uses `src/host_window_sdl3.cpp`, adapted from the pinned
GBARecomp host under its original PolyForm Noncommercial license.

`runtime-sdl3-host.patch` lets GBARecomp accept the parent SDL3 target and select
the project's native host source. The upstream SDL2 source is preserved for
other consumers. The internal legacy availability variable in its CMake file
does not link an SDL2 library. `build.ps1` checks/applies this and the existing
runtime patches before building. Runtime compilation of missing guest functions
defaults off in the PC build; the interpreter bridge still handles missing static
coverage.

The adapter uses SDL3 logical presentation, texture scaling, floating-point
renderer colors, boolean success values, gamepad instance IDs and events.
Audio uses `SDL_AudioStream` for both the adaptive bridge and optional direct
queue path. Direct audio retains its startup buffer before playback. The GBA
simulation, two-seat coordinator and JP resource formats are unchanged.

## Controller database

`config/controllerdb.txt` is a verbatim copy of the community
[SDL_GameControllerDB database](https://github.com/mdqinc/SDL_GameControllerDB),
renamed from upstream `gamecontrollerdb.txt`. Its revision is pinned in
`dependencies.lock.json`; its original license is in
`config/controllerdb-LICENSE.txt` and the packaged `licenses/SDL-GameControllerDB.txt`.

CMake copies it beside both executable builds and into `app/controllerdb.txt`.
Both programs load it after initializing SDL gamepads and before opening them,
using the executable directory regardless of the working directory. No network
download occurs at runtime. The launcher's Controllers page reports load status.
If the file is missing or unreadable, SDL's built-in mappings remain available.
Restart after updating it. Edit the copy in `config/` to preserve custom mappings
through future builds; a build refreshes the packaged file from that copy.

## Validation

The migration build is under `../../work/build/seed-sdl3/`, with staging in
`../../work/sdl3-staging/`. The normal build scripts still use their documented
build directories and package to `app/`.

`seed_sdl3_host_tests` opens a native SDL3 window, checks 4,320 sampled game-texture
pixels and 4,320 JP-selector pixels, tests audio startup/pause/resume/feed, loads
the controller database, and verifies independent virtual gamepads, stable P2
identity after P1 disconnect, and quit events. Software rendering/dummy audio
cover both bridge and direct streams. Direct3D 11/WASAPI cover the normal Windows
presentation path. These checks verify operation, not subjective audio quality.

The native match integration test passed Password Mode entry, JP navigation,
all 24 JP grid choices in both seats, separate ready/cancel, P2 movement and
damage, KO, rematch reset and menu return. Input parity still matches 8,192
original-ROM calls. Launcher platform tests and all five rendered ImGui pages
also passed. Physical controller button testing remains manual.

Example host check (use a scratch output folder):

```powershell
$env:SDL_AUDIO_DRIVER='dummy'
..\..\work\build\seed-sdl3\Release\seed_sdl3_host_tests.exe app/assets/destiny-selection.bin '../../work/sdl3-validation/jp-render.png'
```
