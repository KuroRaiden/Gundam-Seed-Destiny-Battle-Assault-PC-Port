"""Qualify the Training meter against native attacks, reactions and pause.

Uses isolated debug ports/saves. Test-only position writes create repeatable
contact/whiff conditions; the meter itself never writes guest memory.
"""

from game_addresses import Common, Jp
from verify_jp_local_vs import Client
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("exe", "rom", "bios", "out"):
        parser.add_argument("--" + field, type=Path, required=True)
    parser.add_argument("--fighters", type=int, nargs="+", default=list(range(1, 28)))
    parser.add_argument("--port", type=int, default=25692)
    args = parser.parse_args()
    case = args.out.resolve() / ("run-" + str(time.monotonic_ns()))
    case.mkdir(parents=True)
    evidence, failures = [], []
    for fighter in args.fighters:
        folder = case / str(fighter)
        folder.mkdir()
        log = (folder / "native.log").open("w")
        process = subprocess.Popen(
            [
                str(args.exe.resolve()),
                "--training",
                "--fighters",
                str(fighter),
                "8",
                "--rom",
                str(args.rom.resolve()),
                "--bios",
                str(args.bios.resolve()),
                "--bios-hle",
                "--save",
                str(folder / "test.sav"),
                "--tcp",
                str(args.port),
            ],
            cwd=folder,
            stdout=log,
            stderr=log,
            env=dict(os.environ, GBARECOMP_SELFHEAL_RECOMPILE="0"),
        )
        client = None
        try:
            client = Client(args.port, process)
            client.frames(600, 0, 0)
            status = client.status()
            assert status["training"] and status["fighters"] == [fighter, 8], status
            meter = client.cmd("seed_frame_meter")
            assert meter["allowed"] and not meter["visible"], meter
            result = dict(fighter=fighter, attacks=[])
            if fighter == args.fighters[0]:

                def memory_hash():
                    return hashlib.sha256(
                        b"".join(
                            bytes.fromhex(client.cmd(command, addr=start, len=size)["data"])
                            for command, start, size in (
                                ("read_ewram", Common.EWRAM_BEGIN, 262144),
                                ("read_iwram", Common.IWRAM_BEGIN, 32768),
                            )
                        )
                    ).hexdigest()

                before = memory_hash()
                assert client.cmd("seed_frame_meter", action="toggle")["visible"]
                assert memory_hash() == before, "Showing the meter changed guest memory"
                assert not client.cmd("seed_frame_meter", action="toggle")["visible"]
                assert memory_hash() == before, "Hiding the meter changed guest memory"
                result["toggle_guest_memory_unchanged"] = True
            for attack, label, left_x, right_x in (
                (1, "shoot", 200, 240),
                (2, "melee-contact", 200, 240),
                *(
                    ([(2, "melee-whiff", 64, 440), (512, "beam", 200, 280)])
                    if fighter == args.fighters[0]
                    else []
                ),
            ):
                client.frames(180, 0, 0)
                for seat, x in enumerate((left_x, right_x)):
                    client.cmd(
                        "write_mem",
                        addr=Jp.BATTLE_CONTEXT
                        + Jp.FIGHTER_RECORDS_OFFSET
                        + seat * Jp.FIGHTER_STRIDE
                        + 36,
                        hex=struct.pack("<i", x << 16).hex(),
                    )
                client.cmd("seed_frame_meter", action="clear")
                observations = []
                for frame in range(140):
                    client.frames(1, attack if frame < 2 else 0, 0)
                    raw = bytes.fromhex(
                        client.cmd(
                            "read_iwram",
                            addr=Jp.BATTLE_CONTEXT + Jp.FIGHTER_RECORDS_OFFSET,
                            len=192,
                        )["data"]
                    )
                    meter = client.cmd("seed_frame_meter")
                    observations.append(
                        dict(
                            frame=frame,
                            state=[struct.unpack_from("<H", raw, seat * 96)[0] for seat in (0, 1)],
                            phase=[row[-1] if row else None for row in meter["history"]],
                            tick=[
                                struct.unpack_from("<H", raw, seat * 96 + 80)[0] for seat in (0, 1)
                            ],
                            animation=[raw[seat * 96 + 14] for seat in (0, 1)],
                            hp=[struct.unpack_from("<H", raw, seat * 96 + 8)[0] for seat in (0, 1)],
                        )
                    )
                history = meter["history"][0]
                present = set(history)
                issues = []
                if not {1, 2, 3}.issubset(present):
                    issues.append("missing startup/active/recovery")
                summary = meter["summaries"][0]
                if (
                    summary["startup"] is not None
                    and summary["startup"] != summary["preactive"] + 1
                ):
                    issues.append("startup convention")
                if label == "melee-whiff" and any(row["state"][1] == 2 for row in observations):
                    issues.append("whiff fixture unexpectedly hit")
                reactions = sum(row["state"][1] == 2 for row in observations)
                yellow = sum(row["phase"][1] == 4 for row in observations)
                if reactions and not yellow:
                    issues.append("native damage reaction missing yellow")
                if yellow and not reactions:
                    issues.append("yellow without native damage reaction")
                item = dict(
                    label=label,
                    summary=summary,
                    history=meter["history"],
                    projectiles=meter["projectiles"],
                    damage_reaction_frames=reactions,
                    yellow_frames=yellow,
                    issues=issues,
                )
                result["attacks"].append(item)
                if issues:
                    failures.append(dict(fighter=fighter, label=label, issues=issues))
                (folder / (label + "-trace.json")).write_text(
                    json.dumps(observations, indent=2) + "\n"
                )
            # The native practice pause runs its own menu loop, without adding
            # gameplay cells. Resuming must restore native combat stepping.
            client.frames(180, 0, 0)
            client.frames(1, 1, 0)
            client.frames(1, 0, 0)
            assert client.cmd("seed_frame_meter")["history"][0][-1] in (
                1,
                2,
                3,
                7,
            ), "Pause fixture did not begin an attack"
            client.local_tap(8)
            assert client.status()["substate"] == 4, client.status()
            paused = client.cmd("seed_frame_meter")["frames"]
            client.frames(30, 0, 0)
            assert (
                client.cmd("seed_frame_meter")["frames"] == paused
            ), "Native pause appended meter cells"
            client.local_tap(8)
            assert client.status()["substate"] == 2, client.status()
            client.frames(5, 0, 0)  # Resume commits before the next native battle dispatch.
            assert (
                client.cmd("seed_frame_meter")["frames"] > paused
            ), "Meter did not resume with native combat"
            result["native_pause_resume"] = True
            evidence.append(result)
            client.cmd("quit")
            process.wait(timeout=10)
            print(
                json.dumps(
                    dict(
                        fighter=fighter,
                        attacks=len(result["attacks"]),
                        issues=[i for i in failures if i["fighter"] == fighter],
                    )
                ),
                flush=True,
            )
        finally:
            if client:
                client.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            log.close()
    report = dict(
        native_fighters=len(evidence),
        attacks=sum(len(item["attacks"]) for item in evidence),
        cases=evidence,
        failures=failures,
    )
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    assert not failures, failures
    print(
        "PASS: native roster attack phases, reactions, projectile tracks, read-only toggle and Training pause/resume.",
        flush=True,
    )


if __name__ == "__main__":
    main()
