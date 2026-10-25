"""Run with IDA's idapro-enabled Python to create an ARM GBA database.

Usage: python ida_prepare_gba.py ROM_COPY [--output REPORT.json]
All writes go beside ROM_COPY. Use a workspace copy rather than the original.
"""
import idapro
import argparse
import json
from pathlib import Path

import ida_auto
import ida_bytes
import ida_entry
import ida_funcs
import ida_ida
import ida_idp
import ida_name
import ida_segment
import ida_segregs
import idautils


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rom = args.rom.resolve()
    if rom.with_suffix(rom.suffix + ".i64").exists() or rom.with_suffix(".i64").exists():
        raise RuntimeError("Use a fresh workspace ROM copy to avoid replacing an existing database")
    result = idapro.open_database(str(rom), False, '-pARM -T"Binary file"')
    if result:
        raise RuntimeError(f"IDA open_database returned {result}")
    try:
        ida_ida.inf_set_64bit(False)
        ida_ida.inf_set_32bit(True)
        ida_auto.auto_wait()
        seg = ida_segment.get_first_seg()
        if seg is None:
            raise RuntimeError("IDA did not create the cartridge segment")
        ida_segment.set_segm_addressing(seg, 1)
        delta = 0x08000000 - seg.start_ea
        if delta:
            status = ida_segment.rebase_program(delta, ida_segment.MSF_FIXONCE)
            if status:
                raise RuntimeError(f"IDA rebase returned {status}")
        seg = ida_segment.getseg(0x08000000)
        seg.perm = ida_segment.SEGPERM_READ | ida_segment.SEGPERM_EXEC
        seg.update()
        ida_segment.set_segm_name(seg, "ROM")
        ida_segment.set_segm_class(seg, "CODE")
        ida_bytes.del_items(0x08000000, ida_bytes.DELIT_SIMPLE, rom.stat().st_size)
        for start, end, name in [
            (0x02000000, 0x02040000, "EWRAM"),
            (0x03000000, 0x03008000, "IWRAM"),
            (0x04000000, 0x04000400, "IO"),
            (0x05000000, 0x05000400, "PALETTE"),
            (0x06000000, 0x06018000, "VRAM"),
            (0x07000000, 0x07000400, "OAM"),
        ]:
            new = ida_segment.segment_t()
            new.start_ea, new.end_ea, new.bitness = start, end, 1
            new.sel = ida_segment.setup_selector(0)
            new.perm = ida_segment.SEGPERM_READ | ida_segment.SEGPERM_WRITE
            ida_segment.add_segm_ex(new, name, "DATA", ida_segment.ADDSEG_NOSREG)
        for address, name in [
            (0x04000120, "REG_SIOMULTI0"),
            (0x04000128, "REG_SIOCNT"),
            (0x0400012A, "REG_SIOMLT_SEND"),
            (0x04000130, "REG_KEYINPUT"),
            (0x04000132, "REG_KEYCNT"),
            (0x04000134, "REG_RCNT"),
        ]:
            ida_bytes.create_word(address, 2)
            ida_name.set_name(address, name, ida_name.SN_CHECK)
        t_register = ida_idp.str2reg("T")
        ida_segregs.split_sreg_range(0x08000000, t_register, 0, ida_segregs.SR_user)
        ida_segregs.split_sreg_range(0x080000C0, t_register, 0, ida_segregs.SR_user)
        ida_entry.add_entry(0x08000000, 0x08000000, "CartridgeHeaderEntry", True)
        ida_entry.add_entry(0x080000C0, 0x080000C0, "CartridgeStart", True)
        ida_auto.auto_wait()
        report = {
            "processor": ida_ida.inf_get_procname(),
            "is_32bit": ida_ida.inf_is_32bit_exactly(),
            "rom_start": hex(ida_segment.get_segm_by_name("ROM").start_ea),
            "functions": [
                {"address": hex(ea), "name": ida_funcs.get_func_name(ea)}
                for ea in idautils.Functions()
            ],
        }
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: len(v) if k == "functions" else v for k, v in report.items()}))
    finally:
        idapro.close_database(True)


if __name__ == "__main__":
    main()
