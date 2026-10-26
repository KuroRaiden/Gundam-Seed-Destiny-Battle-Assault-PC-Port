"""Build the native JP selector pack from verified donor resources and skin capture."""

from game_addresses import Common, Jp
import argparse
from array import array
import hashlib
from pathlib import Path
import struct
from import_jp_assets import JP_SHA256

TRANSPARENT = 0x8000
SIZES = (
    ((8, 8), (16, 16), (32, 32), (64, 64)),
    ((16, 8), (32, 8), (32, 16), (64, 32)),
    ((8, 16), (8, 32), (16, 32), (32, 64)),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument(
        "--skin", required=True, type=Path, help="240x160 RGB capture with dynamic actors disabled"
    )
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if hashlib.sha256(rom).hexdigest() != JP_SHA256:
        raise ValueError("Expected the verified Japanese B42J ROM")

    def resource(group, index):
        return (args.assets / f"group-{group:02d}-{index:03d}.bin").read_bytes()

    def u32(address):
        return struct.unpack_from("<I", rom, address - Common.ROM_BEGIN)[0]

    def tile_pixel(data, tile, x, y):
        offset = tile * 32 + y * 4 + x // 2
        if not 0 <= offset < len(data):
            raise ValueError("Sprite tile exceeds its resource")
        return (data[offset] >> (4 * (x & 1))) & 15

    def sprite(bank, frame, data, palette):
        table = u32(Jp.SPRITE_BANK_TABLE + 4 * (bank + (bank > 149)))
        address = u32(table + 4 * frame) - Common.ROM_BEGIN
        parts = []
        for index in range(struct.unpack_from("<H", rom, address)[0]):
            a0, a1, tile, x, y = struct.unpack_from("<BBHhh", rom, address + 4 + index * 8)
            width, height = SIZES[a0 >> 6][a1 >> 6]
            parts.append((tile & 1023, x, y, width, height, bool(a1 & 16), bool(a1 & 32)))
        x0 = min(part[1] for part in parts)
        y0 = min(part[2] for part in parts)
        width = max(part[1] + part[3] for part in parts) - x0
        height = max(part[2] + part[4] for part in parts) - y0
        pixels = array("H", [TRANSPARENT]) * (width * height)
        colors = struct.unpack("<16H", palette[:32])
        for tile, px, py, w, h, hflip, vflip in reversed(parts):
            for y in range(h):
                for x in range(w):
                    tx, ty = (w - 1 - x if hflip else x), (h - 1 - y if vflip else y)
                    color = tile_pixel(data, tile + (ty // 8) * (w // 8) + tx // 8, tx % 8, ty % 8)
                    if color:
                        pixels[(py - y0 + y) * width + px - x0 + x] = colors[color] & 0x7FFF
        return width, height, pixels

    raw = args.skin.read_bytes()
    if len(raw) != 240 * 160 * 3:
        raise ValueError("Unexpected JP skin capture dimensions")
    background = array(
        "H",
        (
            (raw[i] >> 3) | ((raw[i + 1] >> 3) << 5) | ((raw[i + 2] >> 3) << 10)
            for i in range(0, len(raw), 3)
        ),
    )
    # The unmodified atlas/map contains every JP portrait. Restore that plane
    # over the reference's save-dependent locked slots; gameplay availability
    # is decided separately by the native roster adapter, never by this art.
    tiles, tilemap = resource(19, 0), resource(19, 1)
    colors = bytes(64) + resource(19, 2)
    map_width, map_height = struct.unpack_from("<II", tilemap)
    if (map_width, map_height) != (30, 20):
        raise ValueError("Unexpected JP portrait tilemap")
    for y in range(160):
        for x in range(240):
            entry = struct.unpack_from("<H", tilemap, 16 + 2 * ((y // 8) * map_width + x // 8))[0]
            tx, ty = (7 - x % 8 if entry & 1024 else x % 8), (7 - y % 8 if entry & 2048 else y % 8)
            index = tile_pixel(tiles, entry & 1023, tx, ty)
            if index:
                offset = (entry >> 12) * 32 + index * 2
                background[y * 240 + x] = struct.unpack_from("<H", colors, offset)[0] & 0x7FFF

    images = [(0, 240, 160, background)]
    for fighter in range(30):
        group, index = (17, fighter) if fighter < 20 else (18, fighter - 20)
        mech = sprite(
            199, 0, resource(group, index), resource(group, index + (20 if group == 17 else 10))
        )
        pilot = sprite(199, 2, resource(4, 22 + fighter), resource(4, 52 + fighter))
        name = sprite(199, 8, resource(4, 82 + fighter), resource(4, 15))
        images.extend(((100 + fighter, *mech), (200 + fighter, *pilot), (300 + fighter, *name)))
    output = bytearray(
        struct.pack("<8sIII32s", b"SEEDJP1\0", 240, 160, len(images), bytes.fromhex(JP_SHA256))
    )
    output.extend(rom[0xD7558:0xD7578])
    for image_id, width, height, pixels in images:
        output.extend(struct.pack("<III", image_id, width, height))
        output.extend(struct.pack("<" + "H" * len(pixels), *pixels))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(output)
    print(f"JP selector pack: {len(images)} images, {len(output)} bytes, 32 original grid slots")


if __name__ == "__main__":
    main()
