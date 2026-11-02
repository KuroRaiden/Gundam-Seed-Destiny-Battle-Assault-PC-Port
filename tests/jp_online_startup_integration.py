"""Check online startup with asymmetric local-play history in the same DLL process."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "bios", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    cases = {"cold": {}, "warm-host": {0: "local"}, "warm-guest": {1: "local"},
             "warm-both": {0: "local", 1: "local"}, "training-host": {0: "training"},
             "training-guest": {1: "training"}, "mixed-history": {0: "training", 1: "local"}}
    parser.add_argument("--case", action="append", choices=cases)
    parser.add_argument("--module", type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    results = []
    expected_initial_crc = None
    for index, name in enumerate(args.case or cases):
        folder = args.out / name
        folder.mkdir()
        processes, logs = [], []
        try:
            port = 29860 + index * 2
            for seat in range(2):
                log = (folder / f"peer{seat}.log").open("wb")
                logs.append(log)
                command = [str(args.build / "seed_jp_online_module_peer.exe"),
                           "--rom", str(args.rom), "--bios", str(args.bios),
                           "--save", str(folder / f"peer{seat}.sav"),
                           "--report", str(folder / f"peer{seat}.json"),
                           "--seat", str(seat), "--net-bind", str(port + seat),
                           "--net-peer", str(port + 1 - seat), "--check-frames", "120",
                           "--fighters", "13", "14", "--colors", "1", "0", "--custom-colors"]
                if args.module:
                    command += ["--module", str(args.module)]
                if seat in cases[name]:
                    command.append("--warmup-" + cases[name][seat])
                    if cases[name][seat] == "local":
                        command += ["--warmup-frames", str(37 + seat * 66)]
                processes.append(subprocess.Popen(command, cwd=args.build, env=env, stdout=log,
                                                 stderr=subprocess.STDOUT,
                                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)))
            codes = [process.wait(timeout=45) for process in processes]
            peers = [json.loads((folder / f"peer{seat}.json").read_text()) for seat in range(2)]
            ok = (codes == [0, 0] and peers[0]["initial_crc"] == peers[1]["initial_crc"]
                  and peers[0]["final_crc"] == peers[1]["final_crc"]
                  and all(p["converged"] and not p["desyncs"] for p in peers))
            if ok:
                if expected_initial_crc is None:
                    expected_initial_crc = peers[0]["initial_crc"]
                ok = peers[0]["initial_crc"] == expected_initial_crc
            results.append({"case": name, "passed": ok, "exit_codes": codes, "peers": peers})
            print(("PASS: " if ok else "FAIL: ") + name, flush=True)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
            for log in logs:
                log.close()
    (args.out / "report.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    assert all(case["passed"] for case in results), "Online startup differed; inspect report and peer logs"


if __name__ == "__main__":
    main()
