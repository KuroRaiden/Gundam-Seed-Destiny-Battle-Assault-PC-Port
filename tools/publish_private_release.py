"""Publish an explicitly requested private GitHub release after a clean commit/tag push.

Credentials are obtained from the existing Git helper, kept only in memory, and
sent only to GitHub API/upload hosts. Assets upload to a draft before publishing.
"""

from audit_release import audit_release_assets
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        raise RuntimeError("Unexpected API redirect; credentials were not forwarded.")


class GitHub:
    def __init__(self):
        result = subprocess.run(
            ["git", "credential", "fill"],
            cwd=ROOT,
            input="protocol=https\nhost=github.com\n\n",
            text=True,
            capture_output=True,
            env=dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never"),
            timeout=25,
        )
        values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
        self.token = values.get("password")
        if not self.token:
            raise RuntimeError("The existing GitHub credential helper is unavailable.")
        self.opener = urllib.request.build_opener(NoRedirect())

    def request(self, method, url, data=None, content_type="application/json"):
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.netloc not in (
            "api.github.com",
            "uploads.github.com",
        ):
            raise RuntimeError("Unexpected release API host.")
        headers = {
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2026-03-10",
            "User-Agent": "GundamSeed-PC-release",
        }
        if data is not None:
            headers["Content-Type"] = content_type
        body = json.dumps(data).encode("utf-8") if isinstance(data, dict) else data
        request = urllib.request.Request(url, data=body, headers=headers, method=method)
        try:
            with self.opener.open(request, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError(f"GitHub release API returned HTTP {error.code}.") from None


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--notes", required=True, type=Path)
    parser.add_argument("--asset", required=True, type=Path, action="append")
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    if git("status", "--porcelain"):
        raise RuntimeError("Commit the reviewed source changes before publishing.")
    expected = "https://github.com/" + args.repo + ".git"
    if git("remote", "get-url", "origin") != expected:
        raise RuntimeError("Release repository does not match origin.")
    commit = git("rev-parse", "HEAD")
    if git("rev-parse", args.tag + "^{commit}") != commit:
        raise RuntimeError("Release tag does not identify the reviewed HEAD.")
    remote = git(
        "ls-remote",
        "origin",
        "refs/heads/main",
        "refs/tags/" + args.tag,
        "refs/tags/" + args.tag + "^{}",
    )
    if f"{commit}\trefs/heads/main" not in remote or not any(
        line.startswith(commit + "\trefs/tags/") for line in remote.splitlines()
    ):
        raise RuntimeError("Push the reviewed main commit and tag before publishing.")
    assets = [path.resolve(strict=True) for path in args.asset]
    if len({path.name for path in assets}) != len(assets):
        raise RuntimeError("Asset names must be distinct.")
    audit_release_assets(assets)
    api = GitHub()
    base = "https://api.github.com/repos/" + args.repo
    repository = api.request("GET", base)
    if not repository.get("private") or not repository.get("permissions", {}).get("push"):
        raise RuntimeError("Expected the authorized private repository with push permission.")
    existing = api.request("GET", base + "/releases?per_page=100")
    if any(item["tag_name"] == args.tag for item in existing):
        raise RuntimeError("Release already exists; refusing to replace it automatically.")
    release = api.request(
        "POST",
        base + "/releases",
        {
            "tag_name": args.tag,
            "target_commitish": commit,
            "name": args.name,
            "body": args.notes.read_text(encoding="utf-8"),
            "draft": True,
            "prerelease": False,
            "generate_release_notes": False,
        },
    )
    upload = release["upload_url"].split("{", 1)[0]
    uploaded = []
    for path in assets:
        content = path.read_bytes()
        digest = "sha256:" + hashlib.sha256(content).hexdigest()
        asset = api.request(
            "POST",
            upload + "?" + urllib.parse.urlencode({"name": path.name}),
            content,
            "application/zip" if path.suffix == ".zip" else "text/plain",
        )
        if (
            asset["state"] != "uploaded"
            or asset["size"] != len(content)
            or asset.get("digest") not in (None, digest)
        ):
            raise RuntimeError("Release asset integrity check failed; release remains draft.")
        uploaded.append(
            {
                "name": asset["name"],
                "size": asset["size"],
                "sha256": digest,
                "url": asset["browser_download_url"],
            }
        )
        print(json.dumps({"uploaded": asset["name"], "size": asset["size"]}), flush=True)
    published = api.request("PATCH", base + "/releases/" + str(release["id"]), {"draft": False})
    checked = api.request("GET", base + "/releases/" + str(release["id"]))
    if checked["draft"] or checked["tag_name"] != args.tag:
        raise RuntimeError("Release publication was not confirmed.")
    result = {
        "url": checked["html_url"],
        "tag": args.tag,
        "commit": commit,
        "private": repository["private"],
        "published": not checked["draft"],
        "assets": [
            {
                "name": asset["name"],
                "size": asset["size"],
                "sha256": asset.get("digest"),
                "url": asset["browser_download_url"],
            }
            for asset in checked["assets"]
        ],
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
