"""Capture the original JP selector's static skin for the native frontend.

Build-time inspection only: the donor reference uses the interpreter backend.
The shipped host runs the US game and renders the resulting art pack directly.
"""

from game_addresses import Jp
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time
import zlib
from import_jp_assets import JP_SHA256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-exe", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--port", type=int, default=24872)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != JP_SHA256:
        raise ValueError("Expected the verified Japanese B42J ROM")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0", GBARECOMP_FORCE_INTERP="1")
    log = (args.out / "reference.log").open("w")
    process = subprocess.Popen(
        [
            str(args.reference_exe.resolve()),
            "--rom",
            str(args.rom.resolve()),
            "--bios",
            str(args.bios.resolve()),
            "--bios-hle",
            "--save",
            str(args.out / "reference.sav"),
            "--tcp",
            str(args.port),
        ],
        cwd=args.out,
        env=env,
        stdout=log,
        stderr=log,
    )
    sock = None
    try:
        for _ in range(100):
            if process.poll() is not None:
                raise RuntimeError(f"Donor reference exited with {process.returncode}")
            try:
                sock = socket.create_connection(("127.0.0.1", args.port), 0.2)
                break
            except OSError:
                time.sleep(0.1)
        if sock is None:
            raise RuntimeError("Donor reference did not start its debug server")
        sock.settimeout(45)
        stream = sock.makefile("rwb", buffering=0)

        def cmd(name, **values):
            stream.write((json.dumps(dict(cmd=name, **values)) + "\n").encode())
            reply = json.loads(stream.readline())
            if not reply.get("ok"):
                raise RuntimeError(f"{name}: {reply}")
            return reply

        def screenshot(name):
            shot = cmd("screenshot")
            if (shot["w"], shot["h"]) != (240, 160):
                raise RuntimeError("Unexpected donor screen dimensions")
            raw = bytes.fromhex(shot["data"])

            def chunk(kind, data):
                return (
                    struct.pack(">I", len(data))
                    + kind
                    + data
                    + struct.pack(">I", zlib.crc32(kind + data))
                )

            rows = b"".join(b"\0" + raw[y * 720 : (y + 1) * 720] for y in range(160))
            png = b"\x89PNG\r\n\x1a\n" + chunk(
                b"IHDR", struct.pack(">IIBBBBB", 240, 160, 8, 2, 0, 0, 0)
            )
            (args.out / (name + ".png")).write_bytes(
                png + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
            )
            return raw

        cmd("run_frames", n=120, keyinput=1023)
        # Verified JP selection state, menu theme/mode and two initial fighters.
        for address, data in (
            (Jp.GAME_STATE, struct.pack("<I", 21)),
            (Jp.SUBSTATE, bytes(4)),
            (Jp.STARTUP_RESET_FIELD, bytes(4)),
            (Jp.SCENE_SETUP_BYTE, b"\x01\x06"),
            (Jp.SELECTED_FIGHTER_1, b"\x01\x08"),
            (Jp.SELECTOR_TRANSIENT_STATE, bytes(13)),
        ):
            cmd("write_mem", addr=address, hex=data.hex())
        for _ in range(5):
            cmd("run_frames", n=100, keyinput=1023)
        if cmd("read_iwram", addr=Jp.SUBSTATE, len=4)["data"] != "08000000":
            raise RuntimeError("Donor selection did not finish initialization")
        screenshot("reference-selection")
        # Disable just the dynamic cursor, mech, pilot and name actors in RAM.
        # Original ROM data is unchanged. Static grid/header/VS art stays intact.
        for actor in list(range(17, 21)) + list(range(33, 45)):
            cmd(
                "write_mem",
                addr=Jp.SELECTOR_ACTOR_RECORDS + actor * Jp.SELECTOR_ACTOR_STRIDE,
                hex="0000",
            )
        cmd("run_frames", n=2, keyinput=1023)
        raw = screenshot("skin")
        (args.out / "skin.rgb").write_bytes(raw)
        (args.out / "capture.json").write_text(
            json.dumps(
                {
                    "donor_sha256": JP_SHA256,
                    "skin_sha256": hashlib.sha256(raw).hexdigest(),
                    "dimensions": [240, 160],
                    "backend": "build-time interpreter reference",
                    "dynamic_actors_disabled": list(range(17, 21)) + list(range(33, 45)),
                },
                indent=2,
            )
            + "\n"
        )
        cmd("quit")
        print("Captured verified JP selector skin (240 x 160).")
    finally:
        if sock:
            sock.close()
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=10)
        log.close()


if __name__ == "__main__":
    main()
