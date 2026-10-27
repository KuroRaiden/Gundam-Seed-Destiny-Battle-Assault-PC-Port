"""Verify Start color choices through Local VS and Training's real US menu routes."""

from game_addresses import Us
from verify_jp_local_vs import Client
from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "jp-rom", "bios", "out"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--port", type=int, default=25240)
    args = parser.parse_args()
    args.out = args.out.resolve()
    case = args.out / ("run-" + str(time.monotonic_ns()))
    case.mkdir(parents=True)
    evidence = []
    for training in (False, True):
        folder = case / ("training" if training else "local")
        folder.mkdir()
        log = (folder / "native.log").open("w")
        process = subprocess.Popen(
            [
                str(args.exe.resolve()),
                "--rom",
                str(args.rom.resolve()),
                "--jp-rom",
                str(args.jp_rom.resolve()),
                "--bios",
                str(args.bios.resolve()),
                "--bios-hle",
                "--save",
                str(folder / "test.sav"),
                "--tcp",
                str(args.port),
                "--jp-tcp",
                str(args.port + 1),
            ],
            cwd=folder,
            stdout=log,
            stderr=log,
            env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
        )
        us = jp = None
        try:
            us = Client(args.port, process)
            us.frames(1500)
            for _ in range(15):
                if us.status()["state"] == 4:
                    break
                us.tap(8)
                us.frames(300)
            for _ in range(15):
                if us.cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03":
                    break
                us.frames(60)
            target = (2, 0 if training else 2)
            for _ in range(12):
                x, y = bytes.fromhex(us.cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
                if (x, y) == target:
                    break
                us.tap((16 if x < 2 else 32) if x != 2 else (128 if y < target[1] else 64))
            assert (x, y) == target
            us.tap(1)
            us.frames(450)
            assert us.status()["active"]
            us.screenshot(folder / "default-selection.png", selector=True)
            for _ in range(3 if training else 5):
                us.local_tap(8)
            if training:
                us.local_tap(1)
                us.local_tap(8)
            else:
                us.local_tap(p2=8)
            selection = us.status()
            chosen = [3 if training else 5, 1]
            assert selection["colors"] == chosen and not selection["ready"][1], selection
            us.screenshot(folder / "color-selection.png", selector=True)
            if training:
                us.local_tap(1)
            else:
                us.local_tap(1, 1)
            with ThreadPoolExecutor(1) as executor:
                advancing = executor.submit(us.frames, 60, 0, 0)
                jp = Client(args.port + 1, process)
                jp.frames(600, 0, 0)
                battle = jp.status()
                assert battle["fighters"] == [26 if training else 27, 8], battle
                assert battle["colors"] == [1, 1] and battle["training"] == training, battle
                jp.screenshot(folder / "battle.png")
                if training:
                    jp.local_tap(8)
                    assert jp.status()["substate"] == 4, jp.status()
                    jp.local_tap(8)
                    assert jp.status()["substate"] == 2, jp.status()
                jp.cmd("quit")
                jp.close()
                jp = None
                advancing.result(timeout=20)
            returned = us.status()
            assert returned["colors"] == chosen and returned["ready"] == [False, False], returned
            evidence.append(
                dict(training=training, selection=selection, battle=battle, returned=returned)
            )
            us.cmd("quit")
            process.wait(timeout=10)
            assert process.returncode == 0
        finally:
            for client in (jp, us):
                if client:
                    client.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            log.close()
    (args.out / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(
        "PASS: Local VS and Training Start choices reach native combat, survive return, and Training Start still pauses/resumes in battle."
    )


if __name__ == "__main__":
    main()
