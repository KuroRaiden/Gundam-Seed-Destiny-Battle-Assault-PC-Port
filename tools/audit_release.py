"""Reject game data, caches and personal files in distributable runtime archives."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

RUNTIME_FILES = {
    "GundamSeedRecomp.exe",
    "GundamSeedLauncher.exe",
    "SeedJpCombat.dll",
    "SDL3.dll",
    "controllerdb.txt",
    "launcher-settings.ini",
    "START HERE.txt",
    "portable-manifest.json",
}
UPDATER_FILES = {"Update-Portable.ps1"}
CRT_FILES = {
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vccorlib140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "vcruntime140_threads.dll",
}
FORBIDDEN_ROM_HASHES = {
    "d7952790a4b82b6d230a4953e4bfe48f6693ddd550f1cf0e7ace74bce95453f8",
    "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb",
}
BIOS_SHA1 = "300c20df6731a33952ded8c436f7f186d25d3492"
EXCLUSIONS = ("roms_included", "bios_included", "personal_data_included")
BRAND_ICON_FILES = {"Haro.ico", "LauncherShip.ico"}
ROOT = Path(__file__).resolve().parents[1]
GAMEFILES_MARKER = "gamefiles/Game_and_Bios_Here.file"
GAMEFILES_MARKER_TEXT = (
    (ROOT / "config/Game_and_Bios_Here.file").read_text(encoding="utf-8").encode("utf-8")
)
PORTABLE_SETTINGS = (ROOT / "config/portable-settings.ini").read_text(encoding="utf-8")
BLANK_SETTINGS = 'us ""\njapanese ""\nbios ""\nscale "3"\n'

APPROVED_BRAND_ICONS = json.loads(
    (Path(__file__).resolve().parents[1] / "config/brand-icons.json").read_text(encoding="utf-8")
)["icons"]

GLYPH_HASHES = json.loads((ROOT / "config/input-prompts.json").read_text())["files"]
GLYPH_FILES = {"resource/input-prompts/" + name for name in GLYPH_HASHES}
THEME_HASHES = json.loads((ROOT / "config/launcher-themes.json").read_text())["files"]
THEME_FILES = {"Themes/" + name for name in THEME_HASHES}
V5_RUNTIME_FILES = {"GundamSeedRecomp.exe", "GundamSeedLauncher.exe", "SeedJpCombat.dll",
                    "SDL3.dll", "resource/controllerdb.txt", "config/launcher-settings.ini",
                    "START HERE.txt", "config/portable-manifest.json"}
V5_UPDATER_FILES = {"config/Update-Portable.ps1"}
V5_BRAND_FILES = {"resource/" + name for name in BRAND_ICON_FILES}


def audit_archive(path, forbidden_inputs=()):
    forbidden = [Path(value).read_bytes() for value in forbidden_inputs]
    members = {}
    root = None
    with zipfile.ZipFile(path) as archive:
        for item in archive.infolist():
            if item.is_dir():
                continue
            name = PurePosixPath(item.filename)
            if (
                name.is_absolute()
                or chr(92) in item.filename
                or ".." in name.parts
                or len(name.parts) < 2
            ):
                raise ValueError("Unsafe archive path")
            if root is None:
                root = name.parts[0]
            if name.parts[0] != root:
                raise ValueError("Expected one portable root folder")
            relative = PurePosixPath(*name.parts[1:]).as_posix()
            if relative in members:
                raise ValueError("Duplicate archive member")
            license_file = (
                ((len(name.parts) == 3 and name.parts[1] == "licenses") or
                 (len(name.parts) == 4 and name.parts[1:3] == ("resource","licenses"))) and name.suffix == ".txt"
            )
            if (
                relative not in RUNTIME_FILES | UPDATER_FILES | CRT_FILES | BRAND_ICON_FILES | V5_RUNTIME_FILES | V5_UPDATER_FILES | V5_BRAND_FILES | GLYPH_FILES | THEME_FILES | {GAMEFILES_MARKER}
                and not license_file
            ):
                raise ValueError("Forbidden release file: " + relative)
            if item.file_size > 100_000_000:
                raise ValueError("Unexpectedly large runtime file")
            data = archive.read(item)
            if (
                hashlib.sha256(data).hexdigest() in FORBIDDEN_ROM_HASHES
                or hashlib.sha1(data).hexdigest() == BIOS_SHA1
            ):
                raise ValueError("Raw game input found in " + relative)
            if any(input_data and input_data in data for input_data in forbidden):
                raise ValueError("Embedded raw game input found in " + relative)
            if name.suffix in (".exe", ".dll") and not data.startswith(b"MZ"):
                raise ValueError("Unexpected non-executable runtime payload")
            if license_file or name.suffix in (".txt", ".ini", ".json", ".ps1"):
                data.decode("utf-8-sig")
                if bytes([0]) in data:
                    raise ValueError("Binary data in a text payload")
            members[relative] = data
        if archive.testzip() is not None:
            raise ValueError("Corrupt release ZIP")
    actual_members = members
    organized = "config/portable-manifest.json" in members
    if organized:
        allowed = V5_RUNTIME_FILES | V5_UPDATER_FILES | V5_BRAND_FILES | GLYPH_FILES | THEME_FILES | CRT_FILES | {GAMEFILES_MARKER}
        if not (V5_RUNTIME_FILES | GLYPH_FILES).issubset(members):
            raise ValueError("Incomplete organized runtime package")
        if any(name not in allowed and not (name.startswith("resource/licenses/") and name.count("/") == 2 and name.endswith(".txt")) for name in members):
            raise ValueError("Unexpected file in organized runtime package")
        for name, value in GLYPH_HASHES.items():
            if hashlib.sha256(members["resource/input-prompts/" + name]).hexdigest() != value:
                raise ValueError("Unapproved input glyph")
        def canonical(name):
            if name in V5_RUNTIME_FILES | V5_UPDATER_FILES | V5_BRAND_FILES:
                return name.split("/",1)[-1] if "/" in name else name
            if name.startswith("resource/licenses/"):
                return name.removeprefix("resource/")
            return name
        members = {canonical(name):data for name,data in members.items()}
    if not organized and any(name.startswith(("resource/","config/")) for name in members):
        raise ValueError("Organized files in legacy package")
    if not RUNTIME_FILES.issubset(members):
        raise ValueError("Incomplete runtime package")
    manifest = json.loads(members["portable-manifest.json"])
    distribution = manifest.get("distribution_format")
    gamefiles = distribution in ("seedpc-runtime-only-v3", "seedpc-runtime-only-v4", "seedpc-runtime-only-v5")
    if distribution not in (
        "seedpc-runtime-only-v1",
        "seedpc-runtime-only-v2",
        "seedpc-runtime-only-v3",
        "seedpc-runtime-only-v4",
        "seedpc-runtime-only-v5",
    ) or any(manifest.get(key) is not False for key in EXCLUSIONS):
        raise ValueError("Package is not marked as runtime-only")
    if organized != (distribution == "seedpc-runtime-only-v5"):
        raise ValueError("Portable layout and format disagree")
    updater = UPDATER_FILES.intersection(members)
    if (distribution in ("seedpc-runtime-only-v4", "seedpc-runtime-only-v5")) != bool(updater):
        raise ValueError("Updater capability and portable format disagree")
    if updater and manifest.get("update_preserves_settings") is not True:
        raise ValueError("Missing settings-preserving update policy")
    icons = BRAND_ICON_FILES.intersection(members)
    present_themes = THEME_FILES.intersection(members)
    if present_themes:
        if present_themes != THEME_FILES:
            raise ValueError("Incomplete launcher themes")
        for name, value in THEME_HASHES.items():
            if hashlib.sha256(members["Themes/" + name]).hexdigest() != value:
                raise ValueError("Unapproved launcher theme file: " + name)
    branded = bool(icons)
    if distribution == "seedpc-runtime-only-v1" and branded:
        raise ValueError("Unexpected branding in a legacy runtime package")
    if distribution == "seedpc-runtime-only-v2" and not branded:
        raise ValueError("Legacy branded package has no icons")
    if branded:
        if (
            icons != BRAND_ICON_FILES
            or manifest.get("generated_art_included") is not True
            or manifest.get("game_asset_caches_included") is not False
            or manifest.get("brand_icons") != APPROVED_BRAND_ICONS
        ):
            raise ValueError("Unexpected branding/art policy")
        for name in BRAND_ICON_FILES:
            if hashlib.sha256(members[name]).hexdigest() != APPROVED_BRAND_ICONS[name]:
                raise ValueError("Unapproved branding icon: " + name)
    elif manifest.get("generated_art_included") is not False:
        raise ValueError("Unexpected art in a runtime package")
    if gamefiles:
        if (
            members.get(GAMEFILES_MARKER) != GAMEFILES_MARKER_TEXT
            or manifest.get("gamefiles_directory") != "gamefiles"
            or manifest.get("game_asset_caches_included") is not False
        ):
            raise ValueError("Missing or altered gamefiles placeholder policy")
    elif GAMEFILES_MARKER in members:
        raise ValueError("Unexpected gamefiles member in a legacy package")
    if manifest.get("asset_generation") != "local_from_user_rom":
        raise ValueError("Missing local asset generation policy")
    expected = manifest["files"]
    if set(expected) != set(actual_members) - {"config/portable-manifest.json" if organized else "portable-manifest.json"}:
        raise ValueError("Unexpected files outside manifest")
    for name, value in expected.items():
        if (
            value["bytes"] != len(actual_members[name])
            or value["sha256"] != hashlib.sha256(actual_members[name]).hexdigest()
        ):
            raise ValueError("Manifest mismatch: " + name)
    if members["launcher-settings.ini"].decode("utf-8").replace("\r\n", "\n") != (
        PORTABLE_SETTINGS if gamefiles else BLANK_SETTINGS
    ):
        raise ValueError("Personal game paths or settings included")
    return {
        "runtime_only": True,
        "organized_folders": organized,
        "files": len(members),
        "roms_included": False,
        "bios_included": False,
        "generated_art_included": branded,
        "brand_icons_included": branded,
        "game_asset_caches_included": False,
        "personal_data_included": False,
        "embedded_input_scan": len(forbidden),
        "gamefiles_placeholder": gamefiles,
        "relative_gamefiles_defaults": gamefiles,
    }


def audit_release_assets(paths):
    paths = [Path(path) for path in paths]
    archives = [path for path in paths if path.suffix.lower() == ".zip"]
    allowed = {path.name for path in archives} | {path.name + ".sha256" for path in archives}
    if (
        not archives
        or len({path.name for path in paths}) != len(paths)
        or any(path.name not in allowed for path in paths)
    ):
        raise ValueError(
            "Release assets must be audited runtime ZIPs and matching checksum files only"
        )
    for archive in archives:
        audit_archive(archive)
        checksum = next((path for path in paths if path.name == archive.name + ".sha256"), None)
        expected = (
            hashlib.file_digest(archive.open("rb"), "sha256").hexdigest() + "  " + archive.name
        )
        if checksum is None or checksum.read_text(encoding="ascii").strip() != expected:
            raise ValueError("Missing or mismatched release checksum")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--forbid-input", type=Path, action="append", default=[])
    args = parser.parse_args()
    print(json.dumps(audit_archive(args.archive, args.forbid_input)), flush=True)


if __name__ == "__main__":
    main()
