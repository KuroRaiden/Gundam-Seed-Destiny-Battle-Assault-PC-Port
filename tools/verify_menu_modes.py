"""Exercise real US menu entries: Online lobby and JP single-player Training."""

from game_addresses import Us
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from verify_jp_local_vs import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "jp-rom", "bios", "out"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--port", type=int, default=24877)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    log = (args.out / "native.log").open("w")
    evidence = []
    us = jp = None
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
            str(args.out / "test.sav"),
            "--tcp",
            str(args.port),
            "--jp-tcp",
            str(args.port + 1),
        ],
        cwd=args.out,
        env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
        stdout=log,
        stderr=log,
    )

    def record(client, label):
        value = dict(label=label, **client.status())
        evidence.append(value)
        print(json.dumps(value), flush=True)
        return value

    def enter(x_target, y_target):
        # Leave scripted two-seat input disabled for the original US menu.
        us.cmd("seed_input", enabled=False)
        for _ in range(15):
            if (
                us.status()["state"] == 4
                and us.cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03"
            ):
                break
            us.frames(60)
        assert us.status()["state"] == 4, "US menu not reached"
        for _ in range(12):
            x, y = bytes.fromhex(us.cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
            if (x, y) == (x_target, y_target):
                break
            us.tap((16 if x < x_target else 32) if x != x_target else (128 if y < y_target else 64))
        assert (x, y) == (x_target, y_target)
        us.tap(1)
        us.frames(450)

    try:
        us = Client(args.port, process)
        us.frames(1500)
        us.tap(8)
        us.frames(450)
        for _ in range(10):
            if us.status()["state"] == 4:
                break
            us.tap(8)
            us.frames(300)
        for index in range(2):
            enter(1, 0)
            s = record(us, "online-entry")
            assert (
                s["active"]
                and s["battle_mode"] == "online"
                and s["state"] == 11
                and s["online_lobby"]
                and not s["network_ready"]
                and s["link"] == 0
            ), s
            us.frames(180, 1, 1)
            s = record(us, "online-confirm-blocked")
            assert s["online_lobby"] and s["ready"] == [False, False] and s["state"] == 11, s
            us.screenshot(args.out / "online-jp-background.png", selector=True)
            us.cmd("seed_online_cancel")
            us.frames(450, 0, 0)
            s = record(us, "online-cancel")
            assert s["state"] == 4 and not s["active"] and not s["online_lobby"], s
        enter(2, 0)
        s = record(us, "training-entry")
        assert (
            s["active"]
            and s["battle_mode"] == "training"
            and s["state"] == 11
            and not s["online_lobby"]
        ), s
        us.local_tap(p2=1)
        assert us.status()["ready"] == [False, False], "P2 changed single-player selection"
        for _ in range(3):
            us.local_tap(32)
        assert us.status()["preview"][0] == 12
        us.local_tap(1)
        assert us.status()["ready"] == [True, False]
        for key in (64, 64, 32, 32, 32):
            us.local_tap(key)
        s = record(us, "training-jp-exclusive-selection")
        assert s["preview"] == [12, 13], s
        us.screenshot(args.out / "training-selection.png", selector=True)
        us.local_tap(1)
        with ThreadPoolExecutor(1) as executor:
            advancing = executor.submit(us.frames, 60, 0, 0)
            jp = Client(args.port + 1, process)
            jp.frames(600, 0, 0)
            s = record(jp, "jp-training")
            assert (
                s["training"] and s["state"] == 8 and s["fighters"] == [13, 14] and min(s["hp"]) > 0
            ), s
            jp.screenshot(args.out / "training-battle.png")
            jp.frames(240, 0, 0)
            s = record(jp, "training-idle")
            assert not s["ended"] and s["state"] == 8, s
            jp.frames(2, 8, 0)
            jp.frames(30, 0, 0)
            s = record(jp, "training-options")
            assert s["substate"] == 4, s
            jp.screenshot(args.out / "training-options.png")
            jp.frames(2, 8, 0)
            jp.frames(60, 0, 0)
            s = record(jp, "training-resumed")
            assert s["state"] == 8 and s["substate"] == 2, s
            jp.frames(2, 4, 0)
            jp.frames(120, 0, 0)
            s = record(jp, "training-reset")
            assert s["state"] == 8 and not s["ended"], s
            jp.cmd("quit")
            jp.close()
            jp = None
            advancing.result(timeout=20)
        s = record(us, "training-return-to-selector")
        assert (
            s["state"] == 11 and s["ready"] == [False, False] and s["battle_mode"] == "training"
        ), s
        us.local_tap(2)
        us.frames(450, 0, 0)
        s = record(us, "training-back-to-menu")
        assert s["state"] == 4 and not s["active"], s
        assert max(s["depth"] for s in evidence) < 32
        us.cmd("quit")
        print(
            "PASS: repeated online lobby entry/cancel, connection gate, JP training roster, one-controller selection, native practice options/reset and menu return."
        )
    finally:
        (args.out / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
        for client in (jp, us):
            if client:
                client.close()
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=10)
        log.close()


if __name__ == "__main__":
    main()
