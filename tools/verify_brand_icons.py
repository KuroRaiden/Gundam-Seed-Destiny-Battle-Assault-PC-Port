"""Check that both original ICOs are embedded unmodified in Windows executables."""

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executables", nargs="+", type=Path)
    args = parser.parse_args()
    dll = ctypes.WinDLL("kernel32", use_last_error=True)
    dll.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD]
    dll.LoadLibraryExW.restype = ctypes.c_void_p
    dll.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    dll.FindResourceW.restype = ctypes.c_void_p
    dll.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    dll.LoadResource.restype = ctypes.c_void_p
    dll.LockResource.argtypes = [ctypes.c_void_p]
    dll.LockResource.restype = ctypes.c_void_p
    dll.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    dll.SizeofResource.restype = wintypes.DWORD
    dll.FreeLibrary.argtypes = [ctypes.c_void_p]
    approved = json.loads((ROOT / "config/brand-icons.json").read_text())["icons"]
    for executable in args.executables:
        module = dll.LoadLibraryExW(str(executable.resolve()), None, 2 | 32)
        assert module, ctypes.get_last_error()
        try:

            def resource(kind, identifier):
                found = dll.FindResourceW(
                    module, ctypes.c_void_p(identifier), ctypes.c_void_p(kind)
                )
                assert found, (executable.name, kind, identifier)
                return ctypes.string_at(
                    dll.LockResource(dll.LoadResource(module, found)),
                    dll.SizeofResource(module, found),
                )

            name = {
                "GundamSeedRecomp.exe": "Haro.ico",
                "GundamSeedLauncher.exe": "LauncherShip.ico",
            }[executable.name]
            for group, name in ((101, name),):
                original = (ROOT / "resources/branding" / name).read_bytes()
                assert hashlib.sha256(original).hexdigest() == approved[name]
                count = struct.unpack_from("<H", original, 4)[0]
                embedded = resource(14, group)
                assert struct.unpack_from("<HHH", embedded) == (0, 1, count)
                for n in range(count):
                    w, h, c, res, planes, bits, length, offset = struct.unpack_from(
                        "<BBBBHHII", original, 6 + n * 16
                    )
                    ew, eh, ec, er, ep, eb, size, image = struct.unpack_from(
                        "<BBBBHHIH", embedded, 6 + n * 14
                    )
                    assert (w, h, c, bits, length) == (ew, eh, ec, eb, size)
                    assert ep == 1  # rc.exe normalizes Pillow's zero plane-count directory field.
                    assert resource(3, image) == original[offset : offset + length]
                print(
                    f"PASS: {executable.name} embeds {name}, all {count} icon sizes byte-identical."
                )
        finally:
            dll.FreeLibrary(module)


if __name__ == "__main__":
    main()
