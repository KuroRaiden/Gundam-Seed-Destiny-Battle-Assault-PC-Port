"""Load and exercise every original JP grid choice in both local player seats."""

import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_jp_local_vs import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "bios", "out"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--port", type=int, default=24874)
    parser.add_argument(
        "--training",
        action="store_true",
        help="Exercise the native JP practice mode instead of local VS",
    )
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    roster = list(range(1, 24)) + [25]
    evidence = []
    for index, left in enumerate(roster):
        right = roster[-1 - index]
        case = args.out / f"{left:02d}-{right:02d}"
        case.mkdir(exist_ok=True)
        log = (case / "native.log").open("w")
        client = None
        process = subprocess.Popen(
            [
                str(args.exe.resolve()),
                *(["--training"] if args.training else []),
                "--fighters",
                str(left),
                str(right),
                "--rom",
                str(args.rom.resolve()),
                "--bios",
                str(args.bios.resolve()),
                "--bios-hle",
                "--save",
                str(case / "test.sav"),
                "--tcp",
                str(args.port),
            ],
            cwd=case,
            env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
            stdout=log,
            stderr=log,
        )
        try:
            client = Client(args.port, process)
            client.frames(600, 0, 0)
            start = client.status()
            assert (
                start["started"]
                and start["state"] == (8 if args.training else 4)
                and start["fighters"] == [left, right]
                and min(start["hp"]) > 0
            ), start
            client.frames(90, 0, 0)
            assert client.status()["hp"] == start["hp"], "CPU attacked during neutral local input"
            client.screenshot(case / "battle.png")
            for p1, p2 in ((16, 32), (1, 2), (2, 1), (256, 512), (512, 256)):
                client.frames(3, 0, 0)
                client.frames(20, p1, p2)
                status = client.status()
                assert status["fighters"] == [left, right] and status["state"] == (
                    8 if args.training else 4
                ), status
            evidence.append(dict(native_ids=[left, right], start=start, after_inputs=status))
            client.cmd("quit")
            process.wait(timeout=15)
            assert process.returncode == 0, process.returncode
            print(f"PASS JP fighters {left} / {right}", flush=True)
        finally:
            if client:
                client.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            log.close()
            (args.out / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(
        "PASS: all 24 original grid fighters loaded in both seats and exercised movement/A/B/L/R."
    )


if __name__ == "__main__":
    main()
