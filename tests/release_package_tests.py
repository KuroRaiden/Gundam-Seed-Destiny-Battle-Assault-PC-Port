"""Verify that release packaging cannot inherit local ROMs/assets/profiles."""

from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from audit_release import audit_archive, audit_release_assets

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="seed-runtime-only-") as folder:
        root = Path(folder)
        app = root / "app"
        crt = root / "crt"
        out = root / "portable-v1.2.3"
        app.mkdir()
        crt.mkdir()
        (app / "assets").mkdir()
        (app / "licenses").mkdir()
        for name in (
            "GundamSeedRecomp.exe",
            "GundamSeedLauncher.exe",
            "SeedJpCombat.dll",
            "SDL3.dll",
        ):
            (app / name).write_bytes(b"MZ-test-runtime")
        (app / "controllerdb.txt").write_text("controller mappings\n")
        (app / "licenses/dependency.txt").write_text("Upstream license\n")
        for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
            (crt / name).write_bytes(b"MZ-test-crt")
        for name in (
            "test.gba",
            "gba_bios.bin",
            "local-vs-jp.sav",
            "controls.ini",
            "player-name.txt",
        ):
            (app / name).write_bytes(b"PRIVATE-LOCAL-FILE")
        (app / "assets/destiny-selection.bin").write_bytes(b"PRIVATE-EXTRACTED-ART")
        (app / "launcher-settings.ini").write_text('us "C:/private/test.gba"\n')
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/build_portable.py"),
                "--app",
                str(app),
                "--launcher",
                str(app / "GundamSeedLauncher.exe"),
                "--crt",
                str(crt),
                "--out",
                str(out),
                "--version",
                "1.2.3",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, result.stderr
        archive = out.with_name(out.name + ".zip")
        assert archive.is_file()
        audit_release_assets([archive, archive.with_suffix(".zip.sha256")])
        for forbidden_asset in (app / "test.gba", app / "gba_bios.bin"):
            try:
                audit_release_assets([forbidden_asset])
            except ValueError:
                pass
            else:
                raise AssertionError("Direct game-data upload was accepted")
        report = audit_archive(archive)
        assert report["runtime_only"]
        with zipfile.ZipFile(archive) as package:
            contents = {item.filename: package.read(item) for item in package.infolist()}
        assert not any(b"PRIVATE-" in data for data in contents.values())
        assert all(not Path(name).suffix.lower() in (".gba", ".bin", ".sav") for name in contents)
        injected = root / "bad.zip"
        for bad_name in (
            "assets/cache.bin",
            "gamefiles/rom.gba",
            "player-name.txt",
            "licenses/../../rom.gba",
            "licenses/back\\slash.txt",
        ):
            with zipfile.ZipFile(injected, "w") as bad:
                for name, data in contents.items():
                    bad.writestr(name, data)
                bad.writestr(out.name + "/" + bad_name, b"PRIVATE-LEAK")
            try:
                audit_archive(injected)
            except ValueError:
                pass
            else:
                raise AssertionError("Forbidden file accepted: " + bad_name)
        print(
            json.dumps(
                {
                    "runtime_only_package": "passed",
                    "inherited_rom_bios_art_save_settings_excluded": "passed",
                    "forbidden_archive_members_rejected": 5,
                    "dotted_version_preserved": True,
                }
            ),
            flush=True,
        )


if __name__ == "__main__":
    main()
