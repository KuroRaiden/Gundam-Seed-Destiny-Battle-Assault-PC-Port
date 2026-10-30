"""Verify that release packaging cannot inherit local ROMs/assets/profiles."""

from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from audit_release import (
    audit_archive,
    audit_release_assets,
    PORTABLE_SETTINGS,
    GAMEFILES_MARKER,
    GAMEFILES_MARKER_TEXT,
)

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
        (app / "gamefiles").mkdir()
        (app / "gamefiles/private.gba").write_bytes(b"PRIVATE-LOCAL-ROM")
        (app / "gamefiles/gba_bios.bin").write_bytes(b"PRIVATE-LOCAL-BIOS")
        (app / "gamefiles/Game_and_Bios_Here.file").write_bytes(b"PRIVATE-LOCAL-NOTE")
        (app / "resource/licenses").mkdir(parents=True)
        (app / "config").mkdir()
        for name in (
            "GundamSeedRecomp.exe",
            "GundamSeedLauncher.exe",
            "SeedJpCombat.dll",
            "SDL3.dll",
        ):
            (app / name).write_bytes(b"MZ-test-runtime")
        (app / "config/launcher-build.json").write_text(json.dumps({
            "version": "1.2.3",
            "sha256": hashlib.sha256((app / "GundamSeedLauncher.exe").read_bytes()).hexdigest(),
        }), encoding="utf-8")
        (app / "config/Update-Portable.ps1").write_bytes((ROOT / "launcher/Update-Portable.ps1").read_bytes())
        (app / "resource/controllerdb.txt").write_text("controller mappings\n")
        (app / "resource/licenses/dependency.txt").write_text("Upstream license\n")
        for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
            (crt / name).write_bytes(b"MZ-test-crt")
        for name in (
            "test.gba",
            "gba_bios.bin",
            "local-vs-jp.sav",
            "controls.ini",
            "ms-colors.ini",
            "player-name.txt",
        ):
            (app / name).write_bytes(b"PRIVATE-LOCAL-FILE")
        (app / "assets/destiny-selection.bin").write_bytes(b"PRIVATE-EXTRACTED-ART")
        (app / "assets/ms-color-previews.bin").write_bytes(b"PRIVATE-MS-PREVIEWS")
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
        assert contents[out.name + "/config/launcher-settings.ini"].decode() == PORTABLE_SETTINGS
        assert contents[out.name + "/" + GAMEFILES_MARKER] == GAMEFILES_MARKER_TEXT
        assert report["gamefiles_placeholder"] and report["relative_gamefiles_defaults"]
        assert all(not Path(name).suffix.lower() in (".gba", ".bin", ".sav") for name in contents)
        injected = root / "bad.zip"
        for bad_name in (
            "assets/cache.bin",
            "ms-colors.ini",
            "gamefiles/rom.gba",
            "gamefiles/personal-note.txt",
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
        # Rewriting the manifest must not permit private paths or a replacement
        # for the fixed placeholder. The folder remains data-free in downloads.
        for relative, payload in (
            ("config/launcher-settings.ini", b'us "C:/private/rom.gba"\n'),
            (GAMEFILES_MARKER, b"PRIVATE-LOCAL-NOTE"),
        ):
            modified = dict(contents)
            manifest_key = out.name + "/config/portable-manifest.json"
            modified[out.name + "/" + relative] = payload
            manifest = json.loads(modified[manifest_key])
            manifest["files"][relative] = {
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            modified[manifest_key] = json.dumps(manifest).encode()
            with zipfile.ZipFile(injected, "w") as bad:
                for name, data in modified.items():
                    bad.writestr(name, data)
            try:
                audit_archive(injected)
            except ValueError:
                pass
            else:
                raise AssertionError("Portable defaults/placeholder accepted private content")
        # Only the two original, explicitly requested branding icons may be
        # added to a runtime-only package; this does not permit other art/caches.
        for name in ("Haro.ico", "LauncherShip.ico"):
            (app / "resource" / name).write_bytes((ROOT / "resources/branding" / name).read_bytes())
        branded = root / "portable-branded"
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
                str(branded),
                "--version",
                "1.2.3",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, result.stderr
        report = audit_archive(branded.with_suffix(".zip"))
        assert report["brand_icons_included"] and not report["game_asset_caches_included"]
        with zipfile.ZipFile(branded.with_suffix(".zip")) as package:
            branded_contents = {item.filename: package.read(item) for item in package.infolist()}
        icon_key = next(name for name in branded_contents if name.endswith("/Haro.ico"))
        manifest_key = next(
            name for name in branded_contents if name.endswith("/portable-manifest.json")
        )
        branded_contents[icon_key] += b"PRIVATE-LEAK"
        manifest = json.loads(branded_contents[manifest_key])
        manifest["files"]["resource/Haro.ico"] = {
            "bytes": len(branded_contents[icon_key]),
            "sha256": hashlib.sha256(branded_contents[icon_key]).hexdigest(),
        }
        branded_contents[manifest_key] = json.dumps(manifest).encode()
        with zipfile.ZipFile(injected, "w") as bad:
            for name, data in branded_contents.items():
                bad.writestr(name, data)
        try:
            audit_archive(injected)
        except ValueError:
            pass
        else:
            raise AssertionError("Modified branding payload bypassed the approved-icon hash")
        print(
            json.dumps(
                {
                    "runtime_only_package": "passed",
                    "inherited_rom_bios_art_save_settings_excluded": "passed",
                    "forbidden_archive_members_rejected": 7,
                    "approved_brand_icons_only": "passed",
                    "modified_icon_rejected": "passed",
                    "dotted_version_preserved": True,
                    "gamefiles_defaults_and_marker": "passed",
                    "private_defaults_and_marker_rejected": "passed",
                }
            ),
            flush=True,
        )


if __name__ == "__main__":
    main()
