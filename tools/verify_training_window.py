"""Check training pause/resume in the real shared SDL3 window and JP module."""

from game_addresses import Us
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
from verify_jp_local_vs import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "rom", "jp-rom", "bios", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--port", type=int, default=24880)
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    evidence = []
    us = jp = None
    passed = False
    log = (args.out / "native.log").open("w")
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
            "--window",
            "--scale",
            "4",
            "--tcp-observe",
            str(args.port),
            "--jp-tcp",
            str(args.port + 1),
        ],
        cwd=args.out,
        stdout=log,
        stderr=log,
        env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
    )

    def wait_for(predicate, label):
        until = time.monotonic() + 15
        while time.monotonic() < until:
            if predicate():
                return
            if process.poll() is not None:
                raise RuntimeError("Game exited: " + label)
            time.sleep(0.1)
        raise RuntimeError("Timed out: " + label)

    def tap(client, key):
        client.cmd("seed_input", p1=key, p2=0)
        time.sleep(0.12)
        client.cmd("seed_input", p1=0, p2=0)
        time.sleep(0.35)

    def record(label):
        item = dict(label=label, us=us.status(), jp=jp.status(), run=jp.cmd("run_status"))
        evidence.append(item)
        print(json.dumps(item), flush=True)
        return item

    try:
        us = Client(args.port, process)
        us.cmd("seed_input", p1=0, p2=0)
        for _ in range(20):
            if us.status()["state"] == 4:
                break
            tap(us, 8)
            time.sleep(0.5)
        wait_for(
            lambda: us.status()["state"] == 4
            and us.cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03",
            "US main menu",
        )
        for _ in range(12):
            x, y = bytes.fromhex(us.cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
            if (x, y) == (2, 0):
                break
            tap(us, (16 if x < 2 else 32) if x != 2 else 64)
        assert (x, y) == (2, 0)
        tap(us, 1)
        wait_for(
            lambda: us.status()["active"] and us.status()["control"] == 3, "JP training selector"
        )
        tap(us, 1)
        tap(us, 1)
        jp = Client(args.port + 1, process)
        wait_for(
            lambda: jp.status()["started"]
            and jp.status()["substate"] == 2
            and jp.status()["round"] == 2,
            "JP training battle ready",
        )
        item = record("physical-input-handoff")
        assert (
            not item["us"]["scripted_input"] and not item["jp"]["scripted_input"]
        ), "Test script still owns the interactive match"
        assert item["jp"]["training"] and item["jp"]["state"] == 8
        for cycle in range(3):
            tap(jp, 8)
            wait_for(lambda: jp.status()["substate"] == 4, "Training options")
            item = record("pause-" + str(cycle))
            before = item["run"]["frame"]
            time.sleep(0.6)
            assert (
                jp.cmd("run_status")["frame"] > before + 10
            ), "Native practice menu froze simulation/event processing"
            tap(jp, 8)
            wait_for(lambda: jp.status()["substate"] == 2, "Training resume")
            item = record("resume-" + str(cycle))
            before = item["run"]["frame"]
            time.sleep(0.6)
            assert jp.cmd("run_status")["frame"] > before + 10, "Training did not resume advancing"
        jp.cmd("seed_input", enabled=False)
        us.cmd("seed_input", enabled=False)
        record("normal-controls-restored")
        passed = True
        print(
            "PASS: real shared-window handoff clears scripted menu input; three native practice pause/resume cycles keep advancing; physical controls restored."
        )
    finally:
        for client in (jp, us):
            if client:
                try:
                    client.cmd("seed_input", enabled=False)
                except (OSError, RuntimeError):
                    pass
                client.close()
        (args.out / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
        (args.out / "process.json").write_text(json.dumps(dict(pid=process.pid)) + "\n")
        if not (passed and args.keep_open):
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        log.close()


if __name__ == "__main__":
    main()
