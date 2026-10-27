"""Check all 27 JP native fighters' palettes and explicit mirror color choices."""

from game_addresses import Common, Jp
from verify_jp_local_vs import Client
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "bios", "out"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--port", type=int, default=25180)
    args = parser.parse_args()
    case = args.out.resolve() / ("run-" + str(time.monotonic_ns()))
    case.mkdir(parents=True)
    rom = args.rom.read_bytes()
    evidence = []
    # Opposite colors load both palettes for every fighter, including all three
    # hidden native IDs. Equal colors prove forced mirror recoloring is gone.
    checks = [(f, f, 0, 1, False) for f in range(1, 28)]
    checks += [
        (1, 1, 0, 0, False),
        (1, 1, 1, 1, False),
        (27, 27, 1, 1, False),
        (24, 26, 1, 1, True),
    ]
    for index, (left, right, color1, color2, training) in enumerate(checks):
        folder = case / str(index)
        folder.mkdir()
        log = (folder / "native.log").open("w")
        command = [
            str(args.exe.resolve()),
            "--rom",
            str(args.rom.resolve()),
            "--bios",
            str(args.bios.resolve()),
            "--bios-hle",
            "--save",
            str(folder / "test.sav"),
            "--tcp",
            str(args.port),
            "--fighters",
            str(left),
            str(right),
            "--colors",
            str(color1),
            str(color2),
        ]
        if training:
            command += ["--training"]
        process = subprocess.Popen(
            command,
            cwd=folder,
            stdout=log,
            stderr=log,
            env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
        )
        client = None
        try:
            client = Client(args.port, process)
            client.frames(600, 0, 0)
            state = client.status()
            assert state["started"] and state["fighters"] == [left, right], state
            assert state["colors"] == [color1, color2] and state["training"] == training, state
            palettes = bytes.fromhex(
                client.cmd("read_pal", addr=Common.PALETTE_BEGIN, len=1024)["data"]
            )
            for seat, (fighter, color) in enumerate(((left, color1), (right, color2))):
                descriptor = (
                    Jp.FIGHTER_DESCRIPTOR_TABLE
                    - Common.ROM_BEGIN
                    + Jp.FIGHTER_DESCRIPTOR_STRIDE * fighter
                )
                pointer = struct.unpack_from("<I", rom, descriptor + 8 + 4 * color)[0]
                start = pointer - Common.ROM_BEGIN
                expected = rom[start + 2 : start + 32]
                offset = 512 + seat * 96
                actual = palettes[offset + 2 : offset + 32]
                assert actual == expected, dict(
                    case=index, seat=seat, fighter=fighter, color=color, state=state
                )
            if index >= 27:
                client.screenshot(folder / "battle.png")
            if training:
                for _ in range(3):
                    client.local_tap(8)
                    client.frames(10, 0, 0)
                assert client.status()["started"], "Training color case stopped"
            evidence.append(
                dict(
                    fighters=[left, right],
                    colors=[color1, color2],
                    training=training,
                    palette_sha256=hashlib.sha256(palettes[512:704]).hexdigest(),
                    verified=True,
                )
            )
            client.cmd("quit")
            process.wait(timeout=10)
            assert process.returncode == 0
            print(
                f"PASS palettes: {left}/{right}, colors {color1}/{color2}, training={training}",
                flush=True,
            )
        finally:
            if client:
                client.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            log.close()
    result = dict(native_fighters=27, distinct_palettes_checked=54, cases=evidence)
    (args.out / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS: all native primary/alternate palettes, equal-color mirrors and JP Training.")


if __name__ == "__main__":
    main()
