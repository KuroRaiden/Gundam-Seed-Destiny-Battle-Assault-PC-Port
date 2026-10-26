"""Run two native JP/Gekko peers and require identical final simulation state."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "rom", "bios", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--port", type=int, default=29070)
    parser.add_argument("--condition-label", default="baseline")
    parser.add_argument(
        "--window",
        action="store_true",
        help="Hidden production surfaces; exercise F4 during a running online match",
    )
    parser.add_argument(
        "--finish-fence",
        action="store_true",
        help="Negotiate a common end boundary instead of a fixed horizon",
    )
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    case = args.out / ("run-" + str(time.monotonic_ns()))
    case.mkdir()
    processes = []
    logs = []
    try:
        for seat in range(2):
            folder = case / str(seat)
            folder.mkdir()
            log = (case / f"peer-{seat}.log").open("w")
            logs.append(log)
            extra = (["--window"] if args.window else []) + (
                ["--finish-fence"] if args.finish_fence else []
            )
            processes.append(
                subprocess.Popen(
                    [
                        str(args.exe.resolve()),
                        "--seat",
                        str(seat),
                        "--net-bind",
                        str(args.port + seat),
                        "--net-peer",
                        str(args.port + 1 - seat),
                        "--check-frames",
                        str(args.frames),
                        "--report",
                        str(case / f"peer-{seat}.json"),
                        "--rom",
                        str(args.rom.resolve()),
                        "--bios",
                        str(args.bios.resolve()),
                        "--bios-hle",
                        "--save",
                        str(folder / "test.sav"),
                    ]
                    + extra,
                    cwd=folder,
                    stdout=log,
                    stderr=log,
                    env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
                )
            )
        until = time.monotonic() + 120
        while any(p.poll() is None for p in processes):
            if any(p.poll() not in (None, 0) for p in processes):
                break
            if time.monotonic() > until:
                raise RuntimeError("Native peer qualification timed out")
            time.sleep(0.2)
        for index, process in enumerate(processes):
            if process.poll() is None:
                process.wait(timeout=5)
            if process.returncode:
                logs[index].flush()
                raise RuntimeError(
                    f'Peer {index} exited {process.returncode}; see {case/f"peer-{index}.log"}'
                )
        peers = [json.loads((case / f"peer-{seat}.json").read_text()) for seat in range(2)]
        for peer in peers:
            if args.window:
                assert (
                    peer["title"].startswith("Kira vs Athrun | Delay 2f | Rollback ")
                    and " | Ping " in peer["title"]
                ), peer
            assert peer["converged"] and peer["desyncs"] == 0, peer
            assert (
                peer["frames"] >= args.frames
                if args.finish_fence
                else peer["frames"] == args.frames
            ), peer
            assert peer["final_crc"] == peer["peer_crc"], peer
        assert peers[0]["initial_crc"] == peers[1]["initial_crc"]
        assert peers[0]["frames"] == peers[1]["frames"]
        assert peers[0]["final_crc"] == peers[1]["final_crc"] and peers[0]["hp"] == peers[1]["hp"]
        result = dict(
            condition_label=args.condition_label,
            ports=[args.port, args.port + 1],
            case=str(case),
            peers=peers,
        )
        (args.out / "report.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result), flush=True)
        print(
            "PASS: both native JP peers reached the same frame, machine checksum and fighter health through real UDP/GekkoNet."
        )
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        for log in logs:
            log.close()


if __name__ == "__main__":
    main()
