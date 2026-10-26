"""Build a runtime-only portable release. Game/BIOS files must never be copied."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from audit_release import audit_archive, CRT_FILES


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app", "launcher", "crt", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--version", default="development")
    parser.add_argument("--source-commit", default="")
    args = parser.parse_args()
    for field in ("app", "launcher", "crt"):
        setattr(args, field, getattr(args, field).resolve(strict=True))
    args.out = args.out.resolve()
    archive = args.out.parent / (args.out.name + ".zip")
    if args.out.exists() or archive.exists():
        raise RuntimeError("Portable output already exists; refusing to replace it")
    required = ["GundamSeedRecomp.exe", "SeedJpCombat.dll", "SDL3.dll", "controllerdb.txt"]
    for name in required:
        if not (args.app / name).is_file():
            raise RuntimeError("Missing runtime file: " + name)
    crt_files = sorted(path for path in args.crt.glob("*.dll") if path.name.lower() in CRT_FILES)
    if not {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}.issubset(
        {path.name.lower() for path in crt_files}
    ):
        raise RuntimeError("Incomplete x64 C++ runtime")
    args.out.mkdir(parents=True)
    for name in required:
        shutil.copy2(args.app / name, args.out / name)
    shutil.copy2(args.launcher, args.out / "GundamSeedLauncher.exe")
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
        'us ""\njapanese ""\nbios ""\nscale "3"\n', encoding="utf-8"
    )
    (args.out / "START HERE.txt").write_text(
        """Gundam SEED Destiny / Battle Assault PC - Portable

1. Extract the complete ZIP to a writable folder.
2. Run GundamSeedLauncher.exe and open Game files.
3. Select your own supported US Battle Assault ROM, JP SEED Destiny ROM,
   and extracted 16 KB GBA BIOS. Verify all three, then Launch game.

This download contains no ROMs, BIOS, saves, player profiles or extracted art.
The JP selector art is generated locally from your Japanese ROM on first
launch. Your original files are read in place and are never modified.
No Python installation, compiler or administrator permission is required.
First launch may take a little longer while the local art cache is prepared.

Settings and saves stay with this folder. Copy your own input files into the
folder if you want their saved paths to remain portable when you move it.
Press F4 during play to remap controls. Online VS uses shareable codes/links;
set Player name before hosting or joining. The title shows names and live
input delay, last rollback length and ping. Invitation registration is optional.
If you move the folder, enable links again from its new location.
""" + f"\nVersion: {args.version}\nSource commit: {args.source_commit}\n",
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
        "schema": 2,
        "distribution_format": "seedpc-runtime-only-v1",
        "version": args.version,
        "source_commit": args.source_commit,
        "platform": "windows-x64",
        "roms_included": False,
        "bios_included": False,
        "generated_art_included": False,
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
