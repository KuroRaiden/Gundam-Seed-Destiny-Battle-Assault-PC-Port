"""Build a runtime-only portable release. Game/BIOS files must never be copied."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from audit_release import (
    audit_archive,
    CRT_FILES,
    BRAND_ICON_FILES,
    APPROVED_BRAND_ICONS,
    PORTABLE_SETTINGS,
    GAMEFILES_MARKER,
    GAMEFILES_MARKER_TEXT,
)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app", "launcher", "crt", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--version", default=(Path(__file__).resolve().parents[1] / "config/version.txt").read_text().strip())
    parser.add_argument("--source-commit", default="")
    args = parser.parse_args()
    for field in ("app", "launcher", "crt"):
        setattr(args, field, getattr(args, field).resolve(strict=True))
    receipt_path = args.launcher.parent / "launcher-build.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("version") != args.version or receipt.get("sha256") != digest(args.launcher):
        raise RuntimeError("Launcher binary/version does not match its build receipt; rebuild before packaging")
    args.out = args.out.resolve()
    archive = args.out.parent / (args.out.name + ".zip")
    if args.out.exists() or archive.exists():
        raise RuntimeError("Portable output already exists; refusing to replace it")
    required = ["GundamSeedRecomp.exe", "SeedJpCombat.dll", "SDL3.dll", "controllerdb.txt", "Update-Portable.ps1"]
    for name in required:
        if not (args.app / name).is_file():
            raise RuntimeError("Missing runtime file: " + name)
    crt_files = sorted(path for path in args.crt.glob("*.dll") if path.name.lower() in CRT_FILES)
    if not {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}.issubset(
        {path.name.lower() for path in crt_files}
    ):
        raise RuntimeError("Incomplete x64 C++ runtime")
    icons = {name for name in BRAND_ICON_FILES if (args.app / name).is_file()}
    if icons and icons != BRAND_ICON_FILES:
        raise RuntimeError("Incomplete branding icons")
    for name in icons:
        if digest(args.app / name) != APPROVED_BRAND_ICONS[name]:
            raise RuntimeError("Unapproved branding icon: " + name)
    args.out.mkdir(parents=True)
    for name in required:
        shutil.copy2(args.app / name, args.out / name)
    shutil.copy2(args.launcher, args.out / "GundamSeedLauncher.exe")
    for name in icons:
        shutil.copy2(args.app / name, args.out / name)
    licenses = args.out / "licenses"
    licenses.mkdir()
    for path in sorted((args.app / "licenses").glob("*.txt")):
        text = path.read_text(encoding="utf-8-sig")
        if chr(0) in text:
            raise RuntimeError("Unexpected binary license file")
        (licenses / path.name).write_text(text, encoding="utf-8")
    for path in crt_files:
        shutil.copy2(path, args.out / path.name)
    (args.out / "launcher-settings.ini").write_text(
        PORTABLE_SETTINGS, encoding="utf-8", newline="\n"
    )
    (args.out / "gamefiles").mkdir()
    (args.out / GAMEFILES_MARKER).write_bytes(GAMEFILES_MARKER_TEXT)
    (args.out / "START HERE.txt").write_text(
        """Gundam SEED Destiny / Battle Assault PC - Portable

1. Extract the complete ZIP to a writable folder.
2. Run GundamSeedLauncher.exe and open Game files.
3. Put your own supported US Battle Assault ROM, JP SEED Destiny ROM, and
   extracted 16 KB GBA BIOS in gamefiles/. The launcher prefills paths there.
   Default names are listed in gamefiles/Game_and_Bios_Here.file.
4. Verify all three in Game files, then Launch game. Browse to select files
   with different names or use files stored in another folder.

This download contains no ROMs, BIOS, saves, player profiles or game asset caches.
The JP selector art and stage previews are generated locally from your Japanese ROM on first
launch. Your original files are read in place and are never modified.
No Python installation, compiler or administrator permission is required.
First launch may take a little longer while the local art cache is prepared.

Settings and saves stay with this folder. Copy your own input files into the
folder if you want their saved paths to remain portable when you move it.
Open MS Colors in the launcher to prepare local battle-sprite previews, edit
colors, and save palettes per player/suit. Online uses your Player 1 profiles
and shares them with your opponent. Both players need this updated build.
Play > Black sprite outlines toggles a simple black border around battle fighters.
Outline opacity sets its strength from 0% invisible to 100% solid black.
Both settings save automatically, apply when launching and only change your display.
The preference saves automatically and takes effect when launching a game.
It only changes your display online; the opponent can choose independently.
Press Start on character select to cycle colors/variants before pressing A.
Each local player uses their own Start binding; in Training, P1 picks both.
In offline Training, F6 toggles hitboxes/hurtboxes. F2 saves a practice
situation; F3 reloads it. Start opens the English pause menu using the US font.
The US font cache is generated locally from your US game file.

The bottom left of the launcher shows its version. Open Updates to check this
project's stable GitHub releases. Download update verifies the package; Install
and restart replaces application files in this folder while preserving settings,
controls, names, colors, saves, game files and locally generated art caches.
Close the game before installing. A rollback copy stays in .updates/.

Press F4 during play to remap controls. In Training, F5 toggles the frame
meter without pausing: green startup, red active, blue recovery, yellow hitstun.
After choosing both fighters, select a stage with left/right and confirm with A.
Player 1 chooses locally; the host chooses online. Random is available.
Online VS offers Quick Match with live queue counts and region choices,
or private Host/Join codes and links;
set Player name before hosting or joining. The title shows names and live
input delay, last rollback length and ping. Invitation registration is optional.
If you move the folder, enable links again from its new location.
Online rooms support continuous rematches: choose fighters and press A again
after each match. The same code stays active until someone leaves the room.
"""
        + (
            "\nIncludes the requested original Haro game and difficulty-screen ship launcher icons.\n"
            if icons
            else ""
        )
        + f"\nVersion: {args.version}\nSource commit: {args.source_commit}\n",
        encoding="utf-8",
    )
    (licenses / "Microsoft-Visual-Cpp-Runtime.txt").write_text(
        """Microsoft Visual C++ Runtime - application-local release x64 DLLs
Copyright Microsoft Corporation. Supplied unmodified.
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
https://visualstudio.microsoft.com/license-terms/
""",
        encoding="utf-8",
    )
    files = {
        path.relative_to(args.out).as_posix(): {
            "bytes": path.stat().st_size,
            "sha256": digest(path),
        }
        for path in sorted(args.out.rglob("*"))
        if path.is_file()
    }
    manifest = {
        "schema": 5,
        "distribution_format": "seedpc-runtime-only-v4",
        "update_preserves_settings": True,
        "gamefiles_directory": "gamefiles",
        "version": args.version,
        "source_commit": args.source_commit,
        "platform": "windows-x64",
        "roms_included": False,
        "bios_included": False,
        "generated_art_included": bool(icons),
        "game_asset_caches_included": False,
        "brand_icons": APPROVED_BRAND_ICONS if icons else {},
        "personal_data_included": False,
        "asset_generation": "local_from_user_rom",
        "relative_game_paths": True,
        "files": files,
    }
    (args.out / "portable-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    with zipfile.ZipFile(
        archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6
    ) as package:
        for path in sorted(args.out.rglob("*")):
            if path.is_file():
                package.write(path, Path(args.out.name) / path.relative_to(args.out))
    audit = audit_archive(archive)
    archive.with_suffix(".zip.sha256").write_text(
        digest(archive) + "  " + archive.name + "\n", encoding="ascii"
    )
    print(
        json.dumps(
            {
                "folder": str(args.out),
                "archive": str(archive),
                "archive_bytes": archive.stat().st_size,
                "zip_crc_verified": True,
                **audit,
            }
        ),
        flush=True,
    )


if __name__ == "__main__":
    main()
