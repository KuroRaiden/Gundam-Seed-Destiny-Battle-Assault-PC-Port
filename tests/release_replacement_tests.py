"""Exercise release promotion and rollback using an in-memory GitHub model."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from replace_release_assets import replace_assets


class FakeAPI:
    def __init__(self, fail_promotion=False):
        self.release = {"id": 1, "name": "0.1.3", "body": "old notes", "draft": False,
                        "upload_url": "https://uploads.github.com/release{?name}",
                        "assets": [{"id": 1, "name": "old.zip"},
                                   {"id": 2, "name": "old.zip.sha256"},
                                   {"id": 3, "name": "unrelated.txt"}]}
        self.next_id = 4
        self.fail_promotion = fail_promotion

    def request(self, method, url, data=None, content_type=None):
        if method == "GET":
            return copy.deepcopy(self.release)
        assets = self.release["assets"]
        if method == "POST":
            name = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)["name"][0]
            asset = {"id": self.next_id, "name": name, "state": "uploaded",
                     "size": len(data), "digest": "sha256:" + hashlib.sha256(data).hexdigest()}
            self.next_id += 1
            assets.append(asset)
            return copy.deepcopy(asset)
        if "/assets/" in url:
            asset = next(item for item in assets if item["id"] == int(url.rsplit("/", 1)[1]))
            if method == "DELETE":
                assets.remove(asset)
                return None
            if self.fail_promotion and data["name"] == "fixed.zip.sha256":
                self.fail_promotion = False
                raise RuntimeError("Injected promotion failure")
            assert not any(item != asset and item["name"] == data["name"] for item in assets)
            asset.update(data)
            return copy.deepcopy(asset)
        self.release.update(data)
        return copy.deepcopy(self.release)


def main():
    with tempfile.TemporaryDirectory(prefix="release-replacement-") as scratch:
        paths = [Path(scratch) / "fixed.zip", Path(scratch) / "fixed.zip.sha256"]
        for path in paths:
            path.write_bytes(b"test upload content")
        for fail in (False, True):
            api = FakeAPI(fail)
            before = copy.deepcopy(api.release)
            try:
                checked = replace_assets(api, "https://api.github.com/repos/test/project", before,
                                         paths, ["old.zip", "old.zip.sha256"], "test",
                                         {"name": "0.1.3 fixed", "body": "fixed notes"})
                assert not fail
                assert {item["name"] for item in checked["assets"]} == {
                    "fixed.zip", "fixed.zip.sha256", "unrelated.txt"}
                assert checked["name"] == "0.1.3 fixed"
            except RuntimeError as error:
                assert fail and str(error) == "Injected promotion failure"
                assert api.release == before
    print("PASS: staged release replacement, unrelated asset preservation and failed-promotion rollback")


if __name__ == "__main__":
    main()
