"""Record ROM identity and candidate hardware-register literals, without patching."""

from game_addresses import Common
import argparse
import hashlib
import json
import struct
from pathlib import Path

REGISTERS = {
    Common.SERIAL_MULTI0: "SIODATA32/SIOMULTI0",
    Common.SERIAL_CONTROL: "SIOCNT",
    Common.SERIAL_SEND: "SIOMLT_SEND",
    Common.KEY_INPUT: "KEYINPUT",
    Common.SERIAL_MODE_CONTROL: "RCNT",
}


def inspect(path):
    data = Path(path).read_bytes()
    if len(data) < 192:
        raise ValueError("File is shorter than a GBA cartridge header")
    checksum = (-sum(data[0xA0:0xBD]) - 0x19) & 255
    literals = []
    for offset in range(0, len(data) - 3, 4):
        value = struct.unpack_from("<I", data, offset)[0]
        if value in REGISTERS:
            literals.append(
                {"rom_address": f"0x{Common.ROM_BEGIN + offset:08X}", "register": REGISTERS[value]}
            )
    return {
        "file": Path(path).name,
        "size": len(data),
        "title": data[0xA0:0xAC].decode("ascii", errors="replace").rstrip("\0 "),
        "game_code": data[0xAC:0xB0].decode("ascii", errors="replace"),
        "version": data[0xBC],
        "header_checksum_valid": checksum == data[0xBD],
        "sha1": hashlib.sha1(data).hexdigest(),
        "sha256": hashlib.sha256(data).hexdigest(),
        "candidate_hardware_literals": literals,
        "qualification": "Literal locations only; these are not verified code references or patch sites.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="+")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps([inspect(p) for p in args.rom], indent=2) + "\n", encoding="utf-8"
    )
