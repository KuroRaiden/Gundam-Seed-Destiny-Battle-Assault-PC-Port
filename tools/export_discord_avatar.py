"""Decode the requested original Lacus portrait locally for a Discord webhook avatar.
The PNG is private generated game art and must not be committed or packaged.
"""
import argparse
import hashlib
from pathlib import Path
import struct
from PIL import Image
from import_jp_assets import resource_offsets, decode_le

PORTRAITS={"classic":76,"pink-dress":80,"space-suit":84,"uniform":88}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--portrait",choices=PORTRAITS,default="uniform")
    args=parser.parse_args()
    rom=args.rom.read_bytes()
    if hashlib.sha256(rom).hexdigest()!="cc310814ce3a134a3d2975bbe0186603a298a5ee15e1795dedb25f07787851eb":
        raise ValueError("Select the supported Japanese Destiny game file")
    _,offsets=resource_offsets(rom,20)
    def resource(index):
        begin,end=offsets[index:index+2]
        return decode_le(rom,begin)[0] if rom[begin:begin+2]==b"Le" else rom[begin:end]
    first=PORTRAITS[args.portrait]
    tiles,tilemap,palette=resource(first),resource(first+1),resource(first+2)
    colors=struct.unpack("<32H",palette)
    columns,rows=struct.unpack_from("<II",tilemap)
    if not 1<=columns<=64 or not 1<=rows<=64 or len(tilemap)!=16+columns*rows*2:
        raise ValueError("Invalid Lacus tilemap")
    portrait=Image.new("RGBA",(columns*8,rows*8))
    for y in range(rows*8):
        for x in range(columns*8):
            entry=struct.unpack_from("<H",tilemap,16+((y//8)*columns+x//8)*2)[0]
            tile=entry&1023
            # This shop map uses one-based tile IDs; ID 1 is the blank tile.
            if tile == 0:continue
            tile-=1
            if (tile+1)*64>len(tiles):raise ValueError("Lacus tile exceeds its resource")
            tx=7-x%8 if entry&1024 else x%8
            ty=7-y%8 if entry&2048 else y%8
            index=tiles[tile*64+ty*8+tx]
            if not index:continue
            if index>=len(colors):raise ValueError("Invalid Lacus palette index")
            color=colors[index]
            portrait.putpixel((x,y),tuple(((color>>s)&31)*255//31 for s in (0,5,10))+(255,))
    # Native maps have a 16-byte header. Starting at byte 8 shifted four tiles
    # into the next row and split the hair/shoulders into detached fragments.
    box=portrait.getbbox()
    if box is None:raise ValueError("No Lacus portrait pixels decoded")
    portrait=portrait.crop(box)
    avatar=Image.new("RGBA",(256,256),(20,27,39,255))
    scale=max(1,min(240//portrait.width,240//portrait.height))
    portrait=portrait.resize((portrait.width*scale,portrait.height*scale),Image.Resampling.NEAREST)
    avatar.alpha_composite(portrait,((256-portrait.width)//2,(256-portrait.height)//2))
    args.out.parent.mkdir(parents=True,exist_ok=True)
    avatar.save(args.out)
    print(f"Original Lacus avatar exported locally: {args.out}")
if __name__=="__main__":main()
