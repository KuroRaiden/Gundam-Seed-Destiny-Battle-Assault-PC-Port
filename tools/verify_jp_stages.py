"""Exercise every normal native JP stage in versus and Training, using scratch saves."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_jp_local_vs import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "rom", "bios", "out"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--port", type=int, default=25010)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    report = []
    for training in (False, True):
        for stage in range(25):
            case = args.out / ("training" if training else "versus") / str(stage)
            case.mkdir(parents=True, exist_ok=True)
            with (case / "native.log").open("w") as log:
                process = subprocess.Popen([
                    str(args.exe.resolve()), "--stage", str(stage),
                    *(["--training"] if training else []), "--fighters", "13", "14",
                    "--rom", str(args.rom.resolve()), "--bios", str(args.bios.resolve()),
                    "--bios-hle", "--save", str(case / "test.sav"), "--tcp", str(args.port)
                ], cwd=case, env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
                    stdout=log, stderr=log)
                client = None
                try:
                    client = Client(args.port, process)
                    client.frames(600, 0, 0)
                    initial = client.status()
                    assert initial["stage"] == stage == initial["requested_stage"], initial
                    assert initial["state"] == (8 if training else 4) and min(initial["hp"]) > 0, initial
                    client.screenshot(case / "battle.png")
                    client.frames(45, 16, 32)
                    client.frames(30, 1, 2)
                    after = client.status()
                    assert after["stage"] == stage and after["state"] == initial["state"], after
                    report.append(dict(training=training, stage=stage, initial=initial, after=after))
                    client.cmd("quit")
                    process.wait(timeout=10)
                    assert process.returncode == 0
                    print(f"PASS {'Training' if training else 'VS'} stage {stage}", flush=True)
                finally:
                    if client:
                        client.close()
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=10)
                    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: all 25 normal JP stages initialized and advanced in VS and Training.")


if __name__ == "__main__":
    main()
