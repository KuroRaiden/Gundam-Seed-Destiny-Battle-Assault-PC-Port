"""Verify US Password -> full JP local combat -> rematch, in one process.

Uses explicit localhost debug ports and scratch saves. Health is reduced only
in the test to shorten KO checks; damage still comes from real P2 attacks.
"""

from game_addresses import Jp, Us
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time
import zlib


class Client:
    def __init__(self, port, process):
        for _ in range(150):
            if process.poll() is not None:
                raise RuntimeError("Native process exited before connection")
            try:
                self.socket = socket.create_connection(("127.0.0.1", port), 0.2)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("No native debug server")
        self.socket.settimeout(55)
        self.stream = self.socket.makefile("rwb", buffering=0)

    def cmd(self, command, **values):
        self.stream.write((json.dumps(dict(cmd=command, **values)) + "\n").encode())
        line = self.stream.readline()
        if not line:
            raise RuntimeError("Native server disconnected")
        reply = json.loads(line)
        assert reply.get("ok"), reply
        return reply

    def frames(self, count, p1=None, p2=0, keys=1023):
        if p1 is not None:
            self.cmd("seed_input", p1=p1, p2=p2)
        return self.cmd("run_frames", n=count, keyinput=keys)

    def tap(self, key):
        self.frames(5, keys=1023 ^ key)
        self.frames(30)

    def local_tap(self, p1=0, p2=0):
        self.frames(2, p1, p2)
        self.frames(2, 0, 0)

    def status(self):
        return self.cmd("seed_status")

    def screenshot(self, path, selector=False):
        shot = self.cmd("seed_selector_pixels" if selector else "screenshot")
        raw = bytes.fromhex(shot["data"])
        w, h = shot["w"], shot["h"]

        def chunk(kind, data):
            return (
                struct.pack(">I", len(data))
                + kind
                + data
                + struct.pack(">I", zlib.crc32(kind + data))
            )

        rows = b"".join(b"\0" + raw[y * w * 3 : (y + 1) * w * 3] for y in range(h))
        path.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows))
            + chunk(b"IEND", b"")
        )

    def close(self):
        self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "jp-rom", "bios", "out"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--port", type=int, default=24871)
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
        assert us.status()["state"] == 4, "US menu not reached"
        for _ in range(12):
            if us.cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03":
                break
            us.frames(60)
        for _ in range(12):
            x, y = bytes.fromhex(us.cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
            if (x, y) == (2, 2):
                break
            us.tap((16 if x < 2 else 32) if x != 2 else (128 if y < 2 else 64))
        assert (x, y) == (2, 2)
        us.screenshot(args.out / "us-password-menu.png")
        us.tap(1)
        us.frames(600)
        selected = record(us, "password-selection")
        assert selected["active"] and selected["state"] == 11 and selected["link"] == 0
        # P1: Strike -> random -> random -> native JP fighter 13.
        for _ in range(3):
            us.local_tap(32)
        # P2: Blitz -> upper row -> upper row -> left -> left -> left.
        for key in (64, 64, 32, 32, 32):
            us.local_tap(p2=key)
        s = record(us, "jp-exclusive-selection")
        assert s["preview"] == [12, 13], s
        us.local_tap(1)
        s = record(us, "p1-ready")
        assert s["ready"] == [True, False]
        us.local_tap(p2=1)
        s = record(us, "both-ready")
        assert s["ready"] == [True, True]
        us.screenshot(args.out / "jp-exclusive-selection.png", selector=True)
        us.frames(30, 0, 0)
        assert us.status()["stage_active"], us.status()
        for _ in range(2):
            us.local_tap(32) # First match: native stage 24; rematch: 22.
        stage = us.status()["stage_cursor"]
        us.screenshot(args.out / f"stage-select-{stage}.png", selector=True)
        with ThreadPoolExecutor(1) as executor:
            advancing = executor.submit(us.frames, 60, 1, 0)
            jp = Client(args.port + 1, process)
            jp.frames(600, 0, 0)
            battle = record(jp, "jp-battle")
            assert battle["fighters"] == [13, 14] and min(battle["hp"]) > 0 and battle["stage"] == stage
            jp.screenshot(args.out / "jp-exclusive-battle.png")
            jp.frames(120, 0, 0)
            assert jp.status()["hp"] == battle["hp"], "P2 AI still active"
            jp.frames(35, 0, 32)
            s = record(jp, "p2-movement")
            assert s["battle_input"] == [0, 32]
            jp.frames(3, 0, 0)
            jp.frames(30, 0, 1)
            s = record(jp, "p2-damage")
            assert s["hp"][0] < battle["hp"][0] and s["hp"][1] == battle["hp"][1]
            # Two real KOs. Wait for each active round before reducing HP.
            for attempt in range(8):
                s = jp.status()
                if s["ended"]:
                    break
                if s["round"] != 5:
                    jp.frames(60, 0, 0)
                    continue
                jp.cmd("write_mem", addr=Jp.PLAYER_1_HEALTH, hex="0100")
                for _ in range(8):
                    jp.frames(3, 0, 0)
                    jp.frames(30, 0, 1)
                    if jp.status()["round"] != 5:
                        break
                record(jp, "ko")
                jp.frames(540, 0, 0)
            assert record(jp, "match-ended")["ended"], "JP match did not finish"
            jp.cmd("quit")
            jp.close()
            jp = None
            advancing.result(timeout=20)
        s = record(us, "rematch")
        assert s["state"] == 11 and s["ready"] == [False, False] and s["link"] == 0
        us.screenshot(args.out / "rematch-selection.png", selector=True)
        # Release the prior stage-confirm script before readying the rematch.
        us.frames(3, 0, 0)
        # Re-enter the same module without starting another process.
        us.local_tap(1, 1)
        us.frames(30, 0, 0)
        assert us.status()["stage_active"], us.status()
        for _ in range(2):
            us.local_tap(32) # First match: native stage 24; rematch: 22.
        stage = us.status()["stage_cursor"]
        us.screenshot(args.out / f"stage-select-{stage}.png", selector=True)
        with ThreadPoolExecutor(1) as executor:
            advancing = executor.submit(us.frames, 60, 1, 0)
            jp = Client(args.port + 1, process)
            jp.frames(480, 0, 0)
            second = record(jp, "second-match")
            assert second["fighters"] == [13, 14] and second["stage"] == stage
            jp.cmd("quit")
            jp.close()
            jp = None
            advancing.result(timeout=20)
        us.local_tap(p2=2)
        us.frames(450, 0, 0)
        s = record(us, "back-to-us-menu")
        assert s["state"] == 4 and not s["active"]
        assert max(item["depth"] for item in evidence) < 32
        us.cmd("quit")
        print(
            "PASS: US Password entry, JP-only confirms, independent P2 movement/damage, two KOs, rematch, module reuse and US menu return."
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
