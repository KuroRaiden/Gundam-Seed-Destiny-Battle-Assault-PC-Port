# Licenses and attribution

This repository integrates the components below. Their license terms apply to
their respective code; full notices are preserved in [licenses/](licenses/).
No separate project-wide license has been selected for original port integration
code. This notice documents existing licenses and does not relicense third-party
code or the original games.

| Component | License | Notice |
| --- | --- | --- |
| [GBARecomp](https://github.com/mstan/gbarecomp) | PolyForm Noncommercial 1.0.0, including its upstream clarification | [GBARecomp.txt](licenses/GBARecomp.txt) |
| [arm-recomp-core](https://github.com/mstan/arm-recomp-core) | MIT | [arm-recomp-core.txt](licenses/arm-recomp-core.txt) |
| [SDL3](https://github.com/libsdl-org/SDL) | zlib | [SDL3.txt](licenses/SDL3.txt) |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | [Dear-ImGui.txt](licenses/Dear-ImGui.txt) |
| [GekkoNet](https://github.com/HeatXD/GekkoNet) | BSD 2-Clause | [GekkoNet.txt](licenses/GekkoNet.txt) |
| [zpp, included by GekkoNet](https://github.com/HeatXD/GekkoNet/tree/9da2363d7c5a68cc77f8dfe2da7cf51d3afe3bee/GekkoLib/thirdparty/zpp) | MIT | [GekkoNet-zpp.txt](licenses/GekkoNet-zpp.txt) |
| [AutoPunch](https://github.com/delthas/autopunch) | MIT | [AutoPunch.txt](licenses/AutoPunch.txt) |
| [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | zlib | [SDL-GameControllerDB.txt](licenses/SDL-GameControllerDB.txt) |
| [JRickey/gba-recomp](https://github.com/JRickey/gba-recomp), code adapted within GBARecomp | MIT OR Apache-2.0 | [MIT](licenses/JRickey-MIT.txt), [Apache-2.0](licenses/JRickey-Apache-2.0.txt) |
| [mGBA](https://github.com/mgba-emu/mgba), BIOS HLE code adapted within GBARecomp | MPL-2.0 for the covered files | [MPL-2.0.txt](licenses/MPL-2.0.txt) |

The pinned revisions and roles are recorded in
[dependencies.lock.json](dependencies.lock.json). GBARecomp's original
[third-party attribution](licenses/GBARecomp-Third-Party-Attribution.txt)
identifies the inherited JRickey and mGBA-derived files.

The exact modified MPL-covered [bios_hle.cpp](third_party/gbarecomp-mpl/bios_hle.cpp)
and [bios_hle.h](third_party/gbarecomp-mpl/bios_hle.h) used by this build are
provided as Source Code Form under MPL-2.0. Their changes cover MSVC portability
and CpuFastSet rounding/timing. They implement BIOS services in software and do
not contain a Nintendo BIOS image. Runtime downloads include the license text
and a source-location notice.

Windows portable builds also include Microsoft's application-local C++ runtime
with its redistribution notice. Development tools such as IDA Pro and Visual
Studio retain their vendors' licenses and are not bundled.

Original game software, ROMs, BIOS images, names and artwork retain their owners'
rights. These software licenses do not license those assets. Players supply
their own verified inputs. Only the two specifically requested branding icons
are included; other game art caches are generated locally.

The F1 movelist uses Kenney Input Prompts 1.5A (CC0-1.0). The supplied notice
is in [resources/input-prompts/Kenney-Input-Prompts.txt](resources/input-prompts/Kenney-Input-Prompts.txt).
The portable includes it under `resource/licenses/`.

Functional command and availability facts are referenced from
[kira_yamato_88's GameFAQs guide, v1.68](https://gamefaqs.gamespot.com/gba/924564-gundam-seed-destiny/faqs/34177).
No move descriptions, strategy text, ratings, or guide narrative are reproduced.
