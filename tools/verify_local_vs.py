"""Exercise the real menu -> Password Mode -> local VS -> rematch path.

Requires a built native host and the user's US ROM/BIOS. Uses the explicitly
enabled localhost debug server; no ROM bytes are changed. KO checks temporarily
reduce a fighter's health before actual P2 attacks to shorten each round.
"""

from game_addresses import Us
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time
import zlib


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--port", type=int, default=24871)
    parser.add_argument(
        "--entry-probe",
        choices=("vs", "password", "preset"),
        help="Inspect one original menu entry without running a local match",
    )
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0")
    log = (args.out / "native.log").open("w")
    launch = [
        str(args.exe.resolve()),
        "--rom",
        str(args.rom.resolve()),
        "--bios",
        str(args.bios.resolve()),
        "--bios-hle",
        "--save",
        str(args.out / "test.sav"),
        "--tcp",
        str(args.port),
    ]
    process = subprocess.Popen(launch, cwd=args.out, env=env, stdout=log, stderr=log)
    sock = None
    evidence = []
    try:
        for _ in range(100):
            require(process.poll() is None, "Native host exited before connecting")
            try:
                sock = socket.create_connection(("127.0.0.1", args.port), 0.2)
                break
            except OSError:
                time.sleep(0.1)
        require(sock is not None, "Native debug server did not start")
        sock.settimeout(45)
        stream = sock.makefile("rwb", buffering=0)

        def cmd(name, **values):
            stream.write((json.dumps(dict(cmd=name, **values)) + "\n").encode())
            line = stream.readline()
            require(bool(line), "Native host disconnected")
            reply = json.loads(line)
            require(reply.get("ok", False), f"{name} failed: {reply}")
            return reply

        def status(label):
            reply = cmd("seed_status")
            evidence.append(dict(label=label, **reply))
            return reply

        def frames(count, keys=1023):
            cmd("run_frames", n=count, keyinput=keys)

        def tap(button):
            frames(5, 1023 ^ button)
            frames(30)

        def local(p1, p2, count=3):
            cmd("seed_input", p1=p1, p2=p2)
            frames(count)

        def screenshot(name, selector=False):
            shot = cmd("seed_selector_pixels" if selector else "screenshot")
            raw = bytes.fromhex(shot["data"])
            width, height = shot["w"], shot["h"]
            scanlines = b"".join(
                b"\0" + raw[y * width * 3 : (y + 1) * width * 3] for y in range(height)
            )

            def chunk(kind, data):
                return (
                    struct.pack(">I", len(data))
                    + kind
                    + data
                    + struct.pack(">I", zlib.crc32(kind + data))
                )

            png = b"\x89PNG\r\n\x1a\n" + chunk(
                b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
            )
            png += chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")
            (args.out / (name + ".png")).write_bytes(png)

        frames(1200)
        for _ in range(8):
            if status("boot/menu")["state"] == 4:
                break
            tap(8)
            frames(300)
        require(status("main-menu")["state"] == 4, "Could not reach the menu through Start")
        for _ in range(12):
            if cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03":
                break
            frames(60)
        # Original 3 x 3 menu: VS is top middle, Preset VS bottom left,
        # Password bottom right. Observe both the cursor and visible selection.
        target = {"vs": (1, 0), "password": (2, 2), "preset": (0, 2)}.get(args.entry_probe, (2, 2))
        for _ in range(12):
            x, y = bytes.fromhex(cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
            if (x, y) == target:
                break
            tap((16 if x < target[0] else 32) if x != target[0] else (128 if y < target[1] else 64))
        require((x, y) == target, "Could not select the requested menu item")
        status("menu-" + (args.entry_probe or "password"))
        evidence[-1].update(menu_entry=args.entry_probe or "password", menu_cursor=[x, y])
        screenshot("menu-entry")
        tap(1)
        if args.entry_probe:
            for _ in range(6):
                frames(60)
                status("entry-" + args.entry_probe)
            screenshot(
                "entry-result", selector=evidence[-1]["active"] and evidence[-1]["state"] == 11
            )
            print(json.dumps(evidence[-1]))
            if args.entry_probe == "vs":
                tap(2)
                frames(240)
                print(json.dumps(status("vs-cancel")))
            cmd("quit")
            return
        frames(240)
        for _ in range(12):
            current = status("selection-wait")
            if current["active"] and current["state"] == 11 and current["control"] == 3:
                break
            frames(60)
        require(
            current["active"] and current["state"] == 11 and current["control"] == 3,
            "Password Mode did not reach local selection",
        )
        require(current["link"] == 0, "Local selection requires a link connection")
        require(
            current["selector"] == "jp-destiny" and current["cursor"] == [13, 22],
            "Local VS is not using the original JP grid",
        )
        # Hover an exclusive donor ID using the actual 2D controls. It must
        # preview correctly without reaching a nonexistent US battle table.
        local(128, 0)
        exclusive = status("jp-exclusive-preview")
        require(exclusive["preview"][0] == 6, "JP vertical grid movement is incorrect")
        local(0, 0)
        local(32, 0)
        require(status("jp-exclusive-slot")["preview"][0] == 15, "Could not reach JP-only preview")
        local(0, 0)
        local(1, 0)
        require(
            status("jp-exclusive-blocked")["ready"] == [False, False],
            "Unported donor fighter was confirmable",
        )
        screenshot("local-vs-jp-exclusive", selector=True)
        # Return to Strike through columns 1,0,7,6,5, then one row up.
        for _ in range(5):
            local(0, 0)
            local(32, 0)
        local(0, 0)
        local(64, 0)
        local(0, 0)
        current = status("jp-classic-return")
        require(current["cursor"] == [13, 22], "JP grid did not return to the classic fighter")
        initial = current["cursor"]
        local(16, 32)
        moved = status("independent-cursors")
        require(
            all(a != b for a, b in zip(initial, moved["cursor"])),
            "Both independent cursors did not move",
        )
        local(0, 0)
        local(1, 0)
        ready = status("p1-ready")
        require(ready["ready"] == [True, False], "P1 confirm changed P2 readiness")
        local(16, 16)
        moved = status("p2-still-selecting")
        require(
            moved["cursor"][0] == ready["cursor"][0] and moved["cursor"][1] != ready["cursor"][1],
            "P1 ready did not lock only its own cursor",
        )
        local(0, 0)
        local(2, 0)
        require(status("p1-cancel-ready")["ready"] == [False, False], "Cancel did not unlock P1")
        local(0, 0)
        local(1, 1)
        require(status("both-ready")["ready"] == [True, True], "Independent confirms failed")
        screenshot("local-vs-selection", selector=True)
        local(0, 0, 300)
        # Retain the stock OS-settings confirmation and versus introduction.
        for _ in range(10):
            if status("prebattle")["state"] == 9:
                break
            local(1, 0, 5)
            local(0, 0, 180)
        local(0, 0, 180)
        battle = status("battle")
        require(battle["state"] == 9 and battle["substate"] == 2, "Battle did not start")
        require(battle["link"] == 0 and min(battle["hp"]) > 0, "Invalid disconnected battle state")
        screenshot("local-vs-battle")
        local(0, 32, 40)
        moving = status("p2-only-movement")
        require(
            moving["raw"] == [0, 32] and moving["battle_input"] == [0, 32],
            "P2 input was mixed into P1",
        )
        local(0, 1, 30)
        attack = status("p2-only-attack")
        require(
            attack["hp"][0] < battle["hp"][0] and attack["hp"][1] == battle["hp"][1],
            "P2-only attack did not damage P1",
        )
        local(0, 0)
        for _ in range(8):
            if status("round")["state"] != 9:
                break
            cmd("write_mem", addr=Us.PLAYER_1_HEALTH, hex="0100")
            local(0, 32, 40)
            local(0, 0, 4)
            local(0, 1, 30)
            status("ko-attack")
            local(0, 0, 480)
        for _ in range(12):
            current = status("rematch-wait")
            if current["state"] == 11 and current["control"] == 3:
                break
            frames(60)
        require(
            current["state"] == 11
            and current["control"] == 3
            and current["ready"] == [False, False],
            "Match end did not reset both selections",
        )
        screenshot("local-vs-rematch", selector=True)
        local(0, 2)
        local(0, 0, 450)
        require(status("cancel-to-menu")["state"] == 4, "P2 cancel did not return to the menu")
        require(not evidence[-1]["active"], "Local input routing remained active in the menu")
        require(all(item["depth"] < 32 for item in evidence), "Native return records accumulated")
        cmd("quit")
        print(
            "PASS: Password Mode entry, independent selection, ready/cancel, P2 battle movement/damage, KO/rematch reset, and menu return."
        )
    finally:
        (args.out / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
        if sock:
            sock.close()
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=10)
        log.close()


if __name__ == "__main__":
    main()
