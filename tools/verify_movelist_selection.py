"""Verify CSS Sword Impulse -> Training -> automatic movelist metadata."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import os
from pathlib import Path
import subprocess
from game_addresses import Us
from verify_jp_local_vs import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "rom", "jp-rom", "bios", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--port", type=int, default=25570)
    args = parser.parse_args()
    folder = args.out.resolve()
    folder.mkdir(parents=True, exist_ok=False)
    (folder / "Logs").mkdir()
    log = (folder / "Logs/native.log").open("wb")
    process = subprocess.Popen(
        [str(args.exe.resolve()), "--rom", str(args.rom.resolve()), "--jp-rom",
         str(args.jp_rom.resolve()), "--bios", str(args.bios.resolve()), "--bios-hle",
         "--save", str(folder / "test.sav"), "--tcp", str(args.port), "--jp-tcp", str(args.port + 1)],
        cwd=folder, stdout=log, stderr=log,
        env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                 SDL_AUDIODRIVER="dummy", GBARECOMP_SELFHEAL_RECOMPILE="0"))
    us = jp = None

    try:
        us = Client(args.port, process, startup_timeout=180)
        us.frames(1500)
        us.tap(8)
        us.frames(450)
        for _ in range(10):
            if us.status()["state"] == 4:
                break
            us.tap(8)
            us.frames(300)
        assert us.status()["state"] == 4
        for _ in range(12):
            if us.cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03":
                break
            us.frames(60)
        for _ in range(12):
            x, y = bytes.fromhex(us.cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
            if (x, y) == (2, 0):
                break
            us.tap((16 if x < 2 else 32) if x != 2 else 64)
        assert (x, y) == (2, 0)
        us.tap(1)
        for _ in range(25):
            if us.status()["active"] and us.status()["control"] == 3:
                break
            us.frames(60)
        assert us.status()["active"] and us.status()["control"] == 3, us.status()
        for key in (64, 32, 32):
            us.local_tap(key)
        selected = us.status()
        assert selected["preview"][0] == 13, selected  # Zero-based JP portrait.
        us.local_tap(1)
        us.local_tap(1)
        us.frames(30, 0, 0)
        assert us.status()["stage_active"]
        with ThreadPoolExecutor(1) as pool:
            advancing = pool.submit(us.frames, 60, 1, 0)
            jp = Client(args.port + 1, process)
            jp.frames(600, 0, 0)
            battle = jp.status()
            assert battle["training"] and battle["fighters"][0] == 14, battle
            assert battle["movelist_fighters"][0] == 14 and battle["movelist_default"] == 14, battle
            jp.cmd("quit")
            jp.close()
            jp = None
            advancing.result(timeout=20)
        report = {"css": selected, "training": battle, "automatic_sword_impulse": True}
        (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print("PASS: selected Sword Impulse in the US-routed JP CSS; Training and the F1 default both use native fighter 14.", flush=True)
    finally:
        for client in (jp, us):
            if client:
                client.close()
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=15)
        log.close()


if __name__ == "__main__":
    main()
