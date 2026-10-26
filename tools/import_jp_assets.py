"""Import verified SEED Destiny UI resources from a local B42J ROM.

Resource addressing: JP routine 0x08000CAC, table 0x08787970.
Le decoding: JP ARM routine 0x08084C18 (copied to IWRAM at runtime).
No cartridge bytes are patched. Generated assets are local build inputs.
"""

from game_addresses import Common, Jp
import argparse
import hashlib
import json
from pathlib import Path
import struct

JP_SHA256 = "cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb"
ARCHIVE_OFFSET = Jp.RESOURCE_ARCHIVE - Common.ROM_BEGIN


def decode_le(data, address):
    if data[address : address + 2] != b"Le":
        raise ValueError(f"Not an Le block at {address:#x}")
    size = int.from_bytes(data[address + 2 : address + 5], "little")
    if not 0 < size <= 0x1000000:
        raise ValueError("Invalid Le output size")
    cursor = address + 6
    result = bytearray()
    while len(result) < size:
        flags = data[cursor]
        cursor += 1
        for _ in range(4):
            kind = flags & 3
            flags >>= 2
            if kind in (2, 3):
                length = 1 if kind == 2 else 3
                if cursor + length > len(data):
                    raise ValueError("Truncated Le literal")
                result.extend(data[cursor : cursor + length])
                cursor += length
            else:
                if kind == 1:
                    value = data[cursor]
                    cursor += 1
                    distance, length = (value & 3) + 1, (value >> 2) + 2
                else:
                    value = struct.unpack_from("<H", data, cursor)[0]
                    cursor += 2
                    distance, length = (value & 0xFFF) + 5, (value >> 12) + 3
                if distance > len(result):
                    raise ValueError("Le back-reference precedes output")
                for _ in range(length):
                    result.append(result[-distance])
            if len(result) > size:
                raise ValueError("Le output exceeds declared size")
            if len(result) == size:
                break
    return bytes(result), cursor - address


def resource_offsets(rom, group):
    table = ARCHIVE_OFFSET + struct.unpack_from("<I", rom, ARCHIVE_OFFSET + 4 * group)[0]
    group_end = ARCHIVE_OFFSET + struct.unpack_from("<I", rom, ARCHIVE_OFFSET + 4 * (group + 1))[0]
    header_size = struct.unpack_from("<H", rom, table)[0]
    if header_size % 2 or not 0 < header_size < 0x1000:
        raise ValueError(f"Unexpected resource index header for group {group}")
    offsets = []
    for index in range(header_size // 2):
        address = table + struct.unpack_from("<H", rom, table + 2 * index)[0]
        if address == group_end:
            return table, offsets  # End sentinel; aligned headers may have padding after it.
        if not table + header_size <= address < group_end:
            raise ValueError(f"Invalid resource address in group {group}")
        offsets.append(address)
    raise ValueError(f"Missing resource end sentinel in group {group}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if hashlib.sha256(rom).hexdigest() != JP_SHA256:
        raise ValueError("Expected the verified Japanese B42J ROM")
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {"rom_sha256": JP_SHA256, "archive_address": "0x08787970", "groups": []}
    oracle_cases = []
    for group in (3, 4, 5, 6, 7, 17, 18, 19):
        table, offsets = resource_offsets(rom, group)
        entries = []
        for index, address in enumerate(offsets):
            item = {"index": index, "address": hex(Common.ROM_BEGIN + address)}
            filename = f"group-{group:02d}-{index:03d}.bin"
            if rom[address : address + 2] == b"Le":
                decoded, consumed = decode_le(rom, address)
                (args.out / filename).write_bytes(decoded)
                item.update(
                    encoding="Le",
                    decoded_size=len(decoded),
                    encoded_size=consumed,
                    sha256=hashlib.sha256(decoded).hexdigest(),
                    file=filename,
                )
                oracle_cases.append(
                    f"{Common.ROM_BEGIN + address:X}\t{len(decoded)}\t{consumed}\t{filename}"
                )
            else:
                # Preserve the physical resource extent, including alignment padding.
                # Its logical structure (palette, sprite metadata, etc.) is resolved by consumers.
                next_group = (
                    ARCHIVE_OFFSET
                    + struct.unpack_from("<I", rom, ARCHIVE_OFFSET + 4 * (group + 1))[0]
                )
                end = min([p for p in offsets if p > address] + [next_group])
                if not address < end <= len(rom):
                    raise ValueError(
                        f"Invalid raw resource extent: group {group}, index {index}, {address:#x}..{end:#x}"
                    )
                raw = rom[address:end]
                (args.out / filename).write_bytes(raw)
                item.update(
                    encoding="raw",
                    physical_size=len(raw),
                    file=filename,
                    sha256=hashlib.sha256(raw).hexdigest(),
                )
            entries.append(item)
        manifest["groups"].append(
            {"group": group, "table_address": hex(Common.ROM_BEGIN + table), "entries": entries}
        )
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (args.out / "oracle-cases.tsv").write_text("\n".join(oracle_cases) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "groups": len(manifest["groups"]),
                "resources": sum(len(g["entries"]) for g in manifest["groups"]),
                "decoded": sum(
                    e["encoding"] == "Le" for g in manifest["groups"] for e in g["entries"]
                ),
            }
        )
    )


if __name__ == "__main__":
    main()
