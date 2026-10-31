"""Verify native recordings and playback with the user's original inputs read in place."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "bios", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

    def run(executable, arguments, log, timeout=40):
        with log.open("wb") as stream:
            result = subprocess.run([str(args.build / executable), *map(str, arguments)],
                                    cwd=args.build, env=env, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=timeout, creationflags=flags)
        if result.returncode:
            raise RuntimeError(f"{executable} failed; inspect {log}")
        return log.read_text(errors="replace")

    run("seed_replay_tests.exe", [args.out / "format"], args.out / "format.log")
    run("seed_replay_native_tests.exe", [args.rom, args.bios, args.out / "local"], args.out / "local.log")
    print("PASS: replay format, corruption rejection and three native local match round-trips", flush=True)

    def peers(folder, first_port, interrupt=False):
        folder.mkdir()
        processes, logs = [], []
        try:
            for seat in range(2):
                log = (folder / f"peer{seat}.log").open("wb")
                logs.append(log)
                command = [str(args.build / "seed_jp_online_module_peer.exe"),
                           "--rom", str(args.rom), "--bios", str(args.bios),
                           "--save", str(folder / f"peer{seat}.sav"),
                           "--report", str(folder / f"peer{seat}.json"), "--seat", str(seat),
                           "--net-bind", str(first_port + seat), "--net-peer", str(first_port + 1 - seat),
                           "--check-frames", "600", "--custom-colors", "--fighters", "13", "14",
                           "--colors", "1", "0"]
                processes.append(subprocess.Popen(command, cwd=args.build, env=env,
                                 stdout=log, stderr=subprocess.STDOUT, creationflags=flags))
            if interrupt:
                time.sleep(3)
                assert processes[1].poll() is None, "Peer ended before interruption test"
                processes[1].terminate()
            codes = [process.wait(timeout=40) for process in processes]
            if interrupt:
                assert codes[0] != 0, "Disconnect was not detected"
                assert ", partial)" in (folder / "peer0.log").read_text(errors="replace")
            else:
                assert codes == [0, 0], codes
                reports = [json.loads((folder / f"peer{seat}.json").read_text()) for seat in range(2)]
                assert all(r["converged"] and not r["desyncs"] for r in reports)
                assert reports[0]["final_crc"] == reports[1]["final_crc"]
            files = sorted((folder / "replays").glob("*.seedreplay"))
            assert len(files) == (1 if interrupt else 2), files
            for i, file in enumerate(files):
                text = run("seed_replay_native_tests.exe", ["--watch", file, args.rom, args.bios],
                           folder / f"playback{i}.log")
                assert "PASS:" in text
            return files, None if interrupt else reports
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
            for log in logs:
                log.close()

    files, reports = peers(args.out / "online", 29270)
    print("PASS: both production online recordings reproduce the match in fresh processes", flush=True)
    text = run("seed_replay_native_tests.exe", ["--watch-window", files[0], args.rom, args.bios],
               args.out / "viewer.log")
    pauses = re.findall(r"replay_pause: (paused|resumed) at frame (\d+)", text)
    assert len(pauses) == 2 and pauses[0][0] == "paused" and pauses[1][0] == "resumed"
    assert pauses[0][1] == pauses[1][1], "Playback advanced while paused"
    print("PASS: real SDL playback, F1/F4 overlays and pause/resume without advancing inputs", flush=True)
    partial, _ = peers(args.out / "interrupted", 29274, interrupt=True)
    print("PASS: disconnected match saves a confirmed partial replay that plays offline", flush=True)
    report = {"local_cases": 3, "online_peers": reports, "viewer_pause": pauses,
              "viewer_f1_f4": True, "partial_replays_verified": len(partial)}
    (args.out / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
