"""Emit functional input facts from the private seed_jp_command_catalog oracle.

The catalog and ROM stay outside the checkout. No ROM bytes, animations, manual
command sequences, or game pseudocode are copied into the generated source.
"""
import argparse
from collections import deque
import json
from pathlib import Path

TOKENS = ((64, "U"), (128, "D"), (32, "B"), (16, "F"),
          (1, "a"), (2, "b"), (512, "l"))


def routes(fighter, mode):
    records = {c["index"]: c for c in fighter["commands"]}
    rows = {r["row"]: r for r in fighter[mode]}
    pending = deque([(0, ())])
    visited = set()
    actions = set()
    result = []
    # Each native variant is displayed once, using its shortest route. A list
    # returning to zero is a cancel loop, not an infinitely long new command.
    while pending:
        start, prefix = pending.popleft()
        row = start
        while row in rows:
            route = rows[row]
            if row not in visited:
                visited.add(row)
                command = records[route["command"]]
                assert command["mask"] & ~0x2F3 == 0
                # A zero attack mask can never satisfy the dispatcher's
                # mandatory new A/B/L press. Do not expose dead padding routes.
                if not command["mask"] & 0x203:
                    row += 1
                    continue
                token = "".join(t for bit, t in TOKENS if command["mask"] & bit)
                assert token and command["mask"] & 0x203
                path = prefix + (token,)
                action = (command["mask"], command["motion"], command["movement"])
                if action not in actions:
                    actions.add(action)
                    result.append((">".join(path), bool(prefix), command["movement"] == 4))
                # Gaia's L changes its entire descriptor. Following a route
                # in the old form's table after transforming reads stale data.
                if not (fighter["fighter"] == 17 and token == "l"):
                    pending.append((route["next"], path))
            row += 1
    return result


def export(data):
    assert {f["fighter"] for f in data} == set(range(1, 28))
    lines = ["// Functional simplified-input facts from the supported JP game's command handlers.",
             "// Reproduce with seed_jp_command_catalog and tools/export_native_movelist.py.",
             "// Native command priority is preserved; > denotes a native follow-up route."]
    for number in range(1, 28):
        merged = {}
        for fighter in (f for f in data if f["fighter"] == number):
            form = ("MS" if fighter["form"] == "MS" else "MA") if number == 17 else ""
            for mode in ("ground", "air"):
                for command, follow, ultimate in routes(fighter, mode):
                    labels = [label for label in (form, "Follow-up" if follow else "",
                              "Berserker active" if ultimate else "") if label]
                    condition = " / ".join(labels)
                    merged.setdefault((command, condition), set()).add(mode)
        lines.append(f"constexpr MoveInput native_{number}[]{{")
        for (command, condition), modes in merged.items():
            air = "Allowed" if len(modes) == 2 else "Only" if "air" in modes else "Ground"
            lines.append(f'    {{"{command}", A::{air}, "{condition}"}},')
        lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    args.destination.write_text(export(json.loads(args.catalog.read_text())), encoding="utf-8")
