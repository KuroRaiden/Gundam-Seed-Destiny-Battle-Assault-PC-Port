"""Connect two real US menus, select JP fighters, run native rollback and return."""

from game_addresses import Us
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import time
from verify_jp_local_vs import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "jp-rom", "bios", "out"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--port", type=int, default=24920)
    parser.add_argument("--udp-port", type=int, default=29070)
    parser.add_argument(
        "--startup-code",
        action="store_true",
        help="Open the guest directly with the host code from a cold start",
    )
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    case = args.out / ("run-" + str(time.monotonic_ns()))
    case.mkdir()
    processes = []
    logs = []
    clients = []
    evidence = []
    invitation = None
    try:
        for seat in range(2):
            folder = case / str(seat)
            folder.mkdir()
            log = (folder / "native.log").open("w")
            logs.append(log)
            startup = ["--join-code", invitation] if seat == 1 and args.startup_code else []
            process = subprocess.Popen(
                [
                    str(args.exe.resolve()),
                    *startup,
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
                    str(args.port + seat * 2),
                    "--jp-tcp",
                    str(args.port + seat * 2 + 1),
                ],
                cwd=folder,
                env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
                stdout=log,
                stderr=log,
            )
            processes.append(process)
            client = Client(args.port + seat * 2, process)
            clients.append(client)
            if seat == 1 and args.startup_code:
                client.frames(2400)
                state = client.status()
                assert state["battle_mode"] == "online" and state["selector"] == "jp-destiny", state
                client.cmd("seed_online_qualification", frames=240)
                continue
            client.frames(1500)
            client.tap(8)
            client.frames(450)
            for _ in range(10):
                if client.status()["state"] == 4:
                    break
                client.tap(8)
                client.frames(300)
            for _ in range(15):
                if client.cmd("read_iwram", addr=Us.MENU_READY_PHASE, len=1)["data"] == "03":
                    break
                client.frames(60)
            for _ in range(12):
                x, y = bytes.fromhex(client.cmd("read_iwram", addr=Us.MENU_CURSOR_X, len=2)["data"])
                if (x, y) == (1, 0):
                    break
                client.tap((16 if x < 1 else 32) if x != 1 else 64)
            assert (x, y) == (1, 0)
            client.tap(1)
            client.frames(450)
            state = client.status()
            assert (
                state["battle_mode"] == "online"
                and state["online_lobby"]
                and state["selector"] == "jp-destiny"
            ), state
            client.cmd("seed_online_qualification", frames=240)
            if seat == 0:
                client.cmd("seed_online_host", address="127.0.0.1", port=args.udp_port, name="Kira")
                client.frames(2, 0, 0)
                invitation = client.status()["online_invitation"]
                assert invitation.startswith("seedpc://join/SEED1-")
        host, guest = clients
        if not args.startup_code:
            guest.cmd("seed_online_join", invite=invitation, name="Athrun")
        until = time.monotonic() + 10
        while True:
            for client in clients:
                client.frames(2, 0, 0)
            states = [client.status() for client in clients]
            if all(s["network_ready"] and not s["online_lobby"] for s in states):
                break
            assert time.monotonic() < until, states
            time.sleep(0.01)
        # Only each machine's Player 1 binding stream is supplied. The joining
        # machine owns the separate Player 2 cursor on both JP selectors.
        host.local_tap(32)
        guest.local_tap(64)
        until = time.monotonic() + 5
        while True:
            for client in clients:
                client.frames(2, 0, 0)
            states = [client.status() for client in clients]
            if states[0]["cursor"] == states[1]["cursor"]:
                break
            assert time.monotonic() < until, states
            time.sleep(0.01)
        assert states[0]["cursor"] == [12, 14], states
        if not args.startup_code:
            assert all(s["players"] == ["Kira", "Athrun"] for s in states), states
        for state in states:
            state.pop("online_invitation", None)
        evidence.append(dict(stage="connected-jp-selection", peers=states))
        # Room handshakes retry on wall-clock intervals. A fast headless
        # batch can finish while both selectors are ready but before the next
        # UDP retry. Keep advancing both peers until combat actually returns.
        until = time.monotonic() + 30
        first = True
        while True:
            with ThreadPoolExecutor(2) as pool:
                jobs = [pool.submit(client.frames, 600, 1 if first else 0, 0) for client in clients]
                for job in jobs:
                    job.result(timeout=50)
            first = False
            states = [client.status() for client in clients]
            if all(s["online_converged"] for s in states):
                break
            assert not all(s["online_lobby"] and not s["network_ready"] for s in states), states
            assert time.monotonic() < until, states
            time.sleep(0.025)
        for state in states:
            assert (
                state["online_converged"]
                and state["online_frames"] == 240
                and not state["online_desyncs"]
            ), state
            assert (
                state["online_crc"] == state["online_peer_crc"]
                and state["online_lobby"]
                and state["state"] == 11
            ), state
            assert state["ready"] == [False, False] and not state["network_ready"], state
        assert states[0]["online_crc"] == states[1]["online_crc"], states
        evidence.append(dict(stage="rollback-and-return", peers=states))
        for client in clients:
            client.cmd("seed_online_cancel")
            client.frames(450, 0, 0)
            state = client.status()
            assert state["state"] == 4 and not state["active"], state
            client.cmd("quit")
        (args.out / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print(json.dumps(evidence), flush=True)
        print(
            "PASS: two real US menu routes, private UDP invitation, independent JP selection, production rollback, identical checksums and menu return."
        )
    finally:
        for client in clients:
            try:
                client.cmd("quit")
            except Exception:
                pass
            client.close()
        for process in processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        for log in logs:
            log.close()


if __name__ == "__main__":
    main()
