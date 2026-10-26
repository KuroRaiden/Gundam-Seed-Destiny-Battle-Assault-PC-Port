"""Check generated symbols and prevent raw GBA addresses returning to port code."""

import io
import json
from pathlib import Path
import re
import tokenize
from generate_address_maps import outputs
from game_addresses import Common, Jp

ROOT = Path(__file__).resolve().parents[1]
CPP_TOKENS = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<raw>[A-Za-z0-9_]*)\(.*?\)(?P=raw)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|(?P<number>\b0[xX][0-9a-fA-F]+)',
    re.S,
)


def is_guest_address(number):
    return Common.EWRAM_BEGIN <= number <= Jp.ROM_END


def main():
    issues = []
    checked = 0
    for path, text in outputs().items():
        if not path.is_file() or path.read_text(encoding="utf-8") != text:
            issues.append(f"Out-of-date generated file: {path.name}")
    for folder in ("src", "tests", "launcher", "tools"):
        for path in (ROOT / folder).rglob("*"):
            if path.suffix not in (".cpp", ".h", ".py") or path.name in (
                "game_addresses.h",
                "game_addresses.py",
            ):
                continue
            checked += 1
            text = path.read_text(encoding="utf-8-sig")
            if path.suffix == ".py":
                for token in tokenize.generate_tokens(io.StringIO(text).readline):
                    if (
                        token.type == tokenize.NUMBER
                        and token.string.lower().startswith("0x")
                        and is_guest_address(int(token.string, 16))
                    ):
                        issues.append(f"{path.relative_to(ROOT)}:{token.start[0]} raw GBA address")
            else:
                for match in CPP_TOKENS.finditer(text):
                    if match["number"] and is_guest_address(int(match["number"], 16)):
                        issues.append(f'{path.relative_to(ROOT)} raw GBA address {match["number"]}')
    if issues:
        raise SystemExit("\n".join(issues))
    print(
        json.dumps(
            {
                "source_files_checked": checked,
                "named_symbols": len(
                    json.loads((ROOT / "analysis/address-map.json").read_text())["entries"]
                ),
                "raw_guest_address_literals": 0,
                "generated_maps_current": True,
            }
        )
    )


if __name__ == "__main__":
    main()
