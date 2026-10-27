"""Replace explicitly named assets on an existing release, staging uploads first.

The immutable tag stays unchanged. The package and release notes identify the
new, pushed source commit. Retired assets are deleted only after verification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.parse
import zipfile

from audit_release import audit_archive, audit_release_assets
from publish_private_release import GitHub, git


def verify_asset(asset, path):
    digest = "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()
    if (asset.get("state") != "uploaded" or asset.get("size") != path.stat().st_size
            or asset.get("digest") != digest):
        raise RuntimeError("Uploaded asset digest/size was not confirmed: " + path.name)


def replace_assets(api, base, release, paths, retired_names, suffix, update):
    """Promote verified uploads, rolling back names/notes if promotion fails."""
    existing = {item["name"]: item for item in release["assets"]}
    if not set(retired_names).issubset(existing):
        raise RuntimeError("An explicitly selected old asset is missing.")
    if any(path.name in existing and path.name not in retired_names for path in paths):
        raise RuntimeError("A new name would overwrite an unrelated asset.")
    temporary_names = [path.name + ".replacement-" + suffix for path in paths]
    backup_names = [name + ".previous-" + suffix for name in retired_names]
    if any(name in existing for name in temporary_names + backup_names):
        raise RuntimeError("A previous replacement needs inspection before retrying.")
    staged, renamed_old, promoted = [], [], []
    release_url = base + "/releases/" + str(release["id"])
    upload = release["upload_url"].split("{", 1)[0]
    try:
        for path, name in zip(paths, temporary_names, strict=True):
            asset = api.request("POST", upload + "?" + urllib.parse.urlencode({"name": name}),
                                path.read_bytes(),
                                "application/zip" if path.suffix == ".zip" else "text/plain")
            staged.append((asset, path))
            verify_asset(asset, path)
        for name, backup in zip(retired_names, backup_names, strict=True):
            item = existing[name]
            api.request("PATCH", base + "/releases/assets/" + str(item["id"]), {"name": backup})
            renamed_old.append(item)
        for asset, path in staged:
            api.request("PATCH", base + "/releases/assets/" + str(asset["id"]), {"name": path.name})
            promoted.append(asset)
        checked = api.request("GET", release_url)
        current = {item["name"]: item for item in checked["assets"]}
        for _, path in staged:
            verify_asset(current[path.name], path)
        api.request("PATCH", release_url, update)
        checked = api.request("GET", release_url)
        if checked["draft"] or any(checked.get(key) != value for key, value in update.items()):
            raise RuntimeError("Updated release details were not confirmed.")
    except Exception:
        # No old asset has been deleted yet. Free canonical names, restore the
        # old downloads and release text, then discard only our staged uploads.
        for asset in reversed(promoted):
            api.request("PATCH", base + "/releases/assets/" + str(asset["id"]),
                        {"name": asset["name"]})
        for item in reversed(renamed_old):
            api.request("PATCH", base + "/releases/assets/" + str(item["id"]),
                        {"name": item["name"]})
        api.request("PATCH", release_url,
                    {"name": release["name"], "body": release["body"]})
        for asset, _ in staged:
            api.request("DELETE", base + "/releases/assets/" + str(asset["id"]))
        raise
    # Past the commit point: replacements and notes have been verified. A
    # cleanup failure leaves backups available; it never deletes the new ZIP.
    for item in renamed_old:
        api.request("DELETE", base + "/releases/assets/" + str(item["id"]))
    return api.request("GET", release_url)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("repo", "tag", "name", "version"):
        parser.add_argument("--" + option, required=True)
    for option in ("notes", "report"):
        parser.add_argument("--" + option, required=True, type=Path)
    for option in ("asset", "forbid-input"):
        parser.add_argument("--" + option, required=True, type=Path, action="append")
    parser.add_argument("--replace-asset", required=True, action="append")
    args = parser.parse_args()
    if git("status", "--porcelain"):
        raise RuntimeError("Commit reviewed source before replacing release assets.")
    if git("remote", "get-url", "origin") != "https://github.com/" + args.repo + ".git":
        raise RuntimeError("Release repository does not match origin.")
    head = git("rev-parse", "HEAD")
    tag_commit = git("rev-parse", args.tag + "^{commit}")
    if git("merge-base", tag_commit, head) != tag_commit:
        raise RuntimeError("Existing tag is not an ancestor of this source.")
    remote = git("ls-remote", "origin", "refs/heads/main", "refs/tags/" + args.tag,
                 "refs/tags/" + args.tag + "^{}")
    if (f"{head}\trefs/heads/main" not in remote or not any(
            line.startswith(tag_commit + "\trefs/tags/") for line in remote.splitlines())):
        raise RuntimeError("Source must be pushed and the existing tag must match.")
    paths = [path.resolve(strict=True) for path in args.asset]
    audit_release_assets(paths)
    for path in paths:
        if path.suffix == ".zip":
            audit_archive(path, args.forbid_input)
            with zipfile.ZipFile(path) as archive:
                name = next(name for name in archive.namelist()
                            if name.endswith("/portable-manifest.json"))
                manifest = json.loads(archive.read(name))
            if manifest.get("source_commit") != head or manifest.get("version") != args.version:
                raise RuntimeError("Package source/version does not match this replacement.")
    notes = args.notes.read_text(encoding="utf-8")
    if "https://github.com/" + args.repo + "/commit/" + head not in notes:
        raise RuntimeError("Replacement notes must link to the actual updated source commit.")
    api = GitHub()
    base = "https://api.github.com/repos/" + args.repo
    repository = api.request("GET", base)
    if (repository.get("full_name", "").casefold() != args.repo.casefold()
            or not repository.get("permissions", {}).get("push")):
        raise RuntimeError("Expected authorized repository with push permission.")
    release = api.request("GET", base + "/releases/tags/" + urllib.parse.quote(args.tag, safe=""))
    if release["draft"] or release["tag_name"] != args.tag:
        raise RuntimeError("Expected an existing published release.")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.with_suffix(".before.json").write_text(json.dumps(release, indent=2) + "\n",
                                                     encoding="utf-8")
    checked = replace_assets(api, base, release, paths, args.replace_asset, head[:12],
                             {"name": args.name, "body": notes})
    names = {item["name"] for item in checked["assets"]}
    if not {path.name for path in paths}.issubset(names):
        raise RuntimeError("Final replacement assets are missing.")
    result = {"url": checked["html_url"], "name": checked["name"], "tag": args.tag,
              "source_commit": head, "immutable_tag_commit": tag_commit,
              "published": not checked["draft"], "private": repository["private"],
              "assets": [{key: item.get(key) for key in
                          ("name", "size", "digest", "browser_download_url")}
                         for item in checked["assets"]]}
    args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
