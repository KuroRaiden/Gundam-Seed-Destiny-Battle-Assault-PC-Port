"""Build the user's personal portable Windows package from verified inputs."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile


def digest(path, algorithm="sha256"):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app", "launcher", "rom", "jp-rom", "bios", "crt", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--version", default="development")
    parser.add_argument("--source-commit", default="")
    args = parser.parse_args()
    for field in ("app", "launcher", "rom", "jp_rom", "bios", "crt"):
        setattr(args, field, getattr(args, field).resolve(strict=True))
    args.out = args.out.resolve()
    archive = args.out.parent / (args.out.name + ".zip")
    if args.out.exists() or archive.exists():
        raise RuntimeError("Portable output already exists; refusing to replace it")
    expected = [
        (
            args.rom,
            4194304,
            "sha256",
            "d7952790a4b82b6d230a4953e4bfe48f6693ddd550f1cf0e7ace74bce95453f8",
        ),
        (
            args.jp_rom,
            8388608,
            "sha256",
            "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb",
        ),
        (args.bios, 16384, "sha1", "300c20df6731a33952ded8c436f7f186d25d3492"),
    ]
    for path, size, algorithm, expected_hash in expected:
        if path.stat().st_size != size or digest(path, algorithm) != expected_hash:
            raise RuntimeError(f"Unsupported game input: {path.name}")
    required = ["GundamSeedRecomp.exe", "SeedJpCombat.dll", "SDL3.dll", "controllerdb.txt"]
    for name in required:
        if not (args.app / name).is_file():
            raise RuntimeError(f"Missing runtime file: {name}")
    crt_files = sorted(args.crt.glob("*.dll"))
    required_crt = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
    if not required_crt.issubset({path.name.lower() for path in crt_files}):
        raise RuntimeError("The x64 release CRT bundle is incomplete")
    args.out.mkdir(parents=True)
    for name in required:
        shutil.copy2(args.app / name, args.out / name)
    shutil.copy2(args.launcher, args.out / "GundamSeedLauncher.exe")
    for name in ("assets", "licenses"):
        shutil.copytree(args.app / name, args.out / name)
    for name in ("controls.ini", "local-vs-jp.sav", "player-name.txt"):
        if (args.app / name).is_file():
            shutil.copy2(args.app / name, args.out / name)
    for path in crt_files:
        shutil.copy2(path, args.out / path.name)
    gamefiles = args.out / "gamefiles"
    gamefiles.mkdir()
    for source, name in (
        (args.rom, "seed-us.gba"),
        (args.jp_rom, "destiny-jp.gba"),
        (args.bios, "gba_bios.bin"),
    ):
        shutil.copy2(source, gamefiles / name)
    us_save = args.rom.with_suffix(".sav")
    if us_save.is_file():
        shutil.copy2(us_save, gamefiles / "seed-us.sav")
    scale = "3"
    settings = args.app / "launcher-settings.ini"
    if settings.is_file():
        for line in settings.read_text(encoding="utf-8-sig").splitlines():
            if line.startswith("scale "):
                scale = line.split(" ", 1)[1].strip().strip('"')
    (args.out / "launcher-settings.ini").write_text(
        'us "gamefiles/seed-us.gba"\njapanese "gamefiles/destiny-jp.gba"\nbios "gamefiles/gba_bios.bin"\nscale '
        + json.dumps(scale)
        + "\n",
        encoding="utf-8",
    )
    (args.out / "START HERE.txt").write_text(
        """Gundam SEED Destiny / Battle Assault PC - Portable

1. Extract the entire ZIP to a writable folder.
2. Run GundamSeedLauncher.exe.
3. Click Launch game, then choose your mode in the game menu.

Windows 10/11, 64-bit. No installer or administrator permission is needed.
The game files, BIOS, SDL3 and C++ runtime DLLs are included in this personal copy.

Move or copy this entire folder together. Settings, controls and saves stay
with the game. Press F4 during play to remap controls.

Online: choose VS Mode, Host, Create code. Share Copy code or Copy link.
Your opponent pastes it in Join. No IP address or port entry is needed.
Set your Player name before creating or joining a room. Online match titles
show both players, input delay, most recent rollback length and measured ping.
Enable invitation links on this PC is optional. If you move this folder,
enable links again from the new location. No fallback relay is configured.
Direct connections can fail on networks that block hole punching.

The launcher uses paths relative to its own folder, so changing drive letters
or launching from another working directory does not require selecting files again.
""",
        encoding="utf-8",
    )
    (args.out / "licenses" / "Microsoft-Visual-Cpp-Runtime.txt").write_text(
        """Microsoft Visual C++ Runtime - application-local release x64 DLLs
Copyright Microsoft Corporation. These DLLs are supplied unmodified.
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
https://visualstudio.microsoft.com/license-terms/
""",
        encoding="utf-8",
    )
    with (args.out / "START HERE.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nVersion: " + args.version + "\nSource commit: " + args.source_commit + "\n")
    files = {}
    for path in sorted(args.out.rglob("*")):
        if path.is_file():
            files[path.relative_to(args.out).as_posix()] = {
                "bytes": path.stat().st_size,
                "sha256": digest(path),
            }
    manifest = {
        "schema": 1,
        "version": args.version,
        "source_commit": args.source_commit,
        "platform": "windows-x64",
        "personal_game_files_included": True,
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
    with zipfile.ZipFile(archive) as package:
        failed = package.testzip()
        if failed:
            raise RuntimeError("ZIP verification failed: " + failed)
    archive.with_suffix(".zip.sha256").write_text(
        digest(archive) + "  " + archive.name + "\n", encoding="ascii"
    )
    print(
        json.dumps(
            {
                "folder": str(args.out),
                "archive": str(archive),
                "archive_bytes": archive.stat().st_size,
                "unpacked_bytes": sum(item["bytes"] for item in files.values()),
                "files": len(files),
                "zip_crc_verified": True,
            }
        ),
        flush=True,
    )


if __name__ == "__main__":
    main()
