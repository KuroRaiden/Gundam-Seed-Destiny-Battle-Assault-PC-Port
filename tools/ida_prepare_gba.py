"""Run with IDA's idapro-enabled Python to create an ARM GBA database.

Usage: python ida_prepare_gba.py ROM_COPY [--output REPORT.json]
All writes go beside ROM_COPY. Use a workspace copy rather than the original.
"""

from game_addresses import Common
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
        delta = Common.ROM_BEGIN - seg.start_ea
        if delta:
            status = ida_segment.rebase_program(delta, ida_segment.MSF_FIXONCE)
            if status:
                raise RuntimeError(f"IDA rebase returned {status}")
        seg = ida_segment.getseg(Common.ROM_BEGIN)
        seg.perm = ida_segment.SEGPERM_READ | ida_segment.SEGPERM_EXEC
        seg.update()
        ida_segment.set_segm_name(seg, "ROM")
        ida_segment.set_segm_class(seg, "CODE")
        ida_bytes.del_items(Common.ROM_BEGIN, ida_bytes.DELIT_SIMPLE, rom.stat().st_size)
        for start, end, name in [
            (Common.EWRAM_BEGIN, Common.EWRAM_END, "EWRAM"),
            (Common.IWRAM_BEGIN, Common.IWRAM_END, "IWRAM"),
            (Common.IO_BEGIN, Common.IO_END, "IO"),
            (Common.PALETTE_BEGIN, Common.PALETTE_END, "PALETTE"),
            (Common.VRAM_BEGIN, Common.VRAM_END, "VRAM"),
            (Common.OAM_BEGIN, Common.OAM_END, "OAM"),
        ]:
            new = ida_segment.segment_t()
            new.start_ea, new.end_ea, new.bitness = start, end, 1
            new.sel = ida_segment.setup_selector(0)
            new.perm = ida_segment.SEGPERM_READ | ida_segment.SEGPERM_WRITE
            ida_segment.add_segm_ex(new, name, "DATA", ida_segment.ADDSEG_NOSREG)
        for address, name in [
            (Common.SERIAL_MULTI0, "REG_SIOMULTI0"),
            (Common.SERIAL_CONTROL, "REG_SIOCNT"),
            (Common.SERIAL_SEND, "REG_SIOMLT_SEND"),
            (Common.KEY_INPUT, "REG_KEYINPUT"),
            (Common.KEY_INTERRUPT_CONTROL, "REG_KEYCNT"),
            (Common.SERIAL_MODE_CONTROL, "REG_RCNT"),
        ]:
            ida_bytes.create_word(address, 2)
            ida_name.set_name(address, name, ida_name.SN_CHECK)
        t_register = ida_idp.str2reg("T")
        ida_segregs.split_sreg_range(Common.ROM_BEGIN, t_register, 0, ida_segregs.SR_user)
        ida_segregs.split_sreg_range(Common.ROM_CODE_ENTRY, t_register, 0, ida_segregs.SR_user)
        ida_entry.add_entry(Common.ROM_BEGIN, Common.ROM_BEGIN, "CartridgeHeaderEntry", True)
        ida_entry.add_entry(Common.ROM_CODE_ENTRY, Common.ROM_CODE_ENTRY, "CartridgeStart", True)
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
