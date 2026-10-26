"""Batch IDAPython operations and evidence export for the copied GBA databases."""

from game_addresses import Common, Jp
import idapro
import argparse
import json
from pathlib import Path

import ida_auto
import ida_bytes
import ida_funcs
import ida_hexrays
import ida_ida
import ida_idp
import ida_lines
import ida_name
import ida_segregs
import ida_ua
import idautils


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("jobs", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    jobs = json.loads(args.jobs.read_text(encoding="utf-8"))
    if idapro.open_database(str(args.database.resolve()), False):
        raise RuntimeError("Could not open IDA database")
    report = []
    try:
        ida_ida.inf_set_64bit(False)
        ida_ida.inf_set_32bit(True)
        ida_auto.auto_wait()
        for job in jobs:
            kind = job["kind"]
            if kind == "function":
                ea = int(job["address"], 0)
                if job.get("replace"):
                    existing = ida_funcs.get_func(ea)
                    if existing:
                        ida_funcs.del_func(existing.start_ea)
                ida_segregs.split_sreg_range(
                    ea, ida_idp.str2reg("T"), int(job.get("thumb", True)), ida_segregs.SR_user
                )
                end = int(job["end"], 0) if job.get("end") else ida_idaapi.BADADDR
                ida_ua.create_insn(ea)
                ok = ida_funcs.add_func(ea, end)
                if job.get("name"):
                    ida_name.set_name(ea, job["name"], ida_name.SN_CHECK)
                report.append({"job": job, "created": ok})
            elif kind == "name":
                ea = int(job["address"], 0)
                report.append(
                    {"job": job, "ok": ida_name.set_name(ea, job["name"], ida_name.SN_CHECK)}
                )
            elif kind == "comment":
                report.append(
                    {
                        "job": job,
                        "ok": ida_bytes.set_cmt(int(job["address"], 0), job["text"], False),
                    }
                )
            elif kind == "table":
                ea = int(job["address"], 0)
                rows = []
                for index in range(job["count"]):
                    slot = ea + index * 4
                    value = ida_bytes.get_dword(slot)
                    ida_bytes.create_dword(slot, 4)
                    rows.append({"slot": hex(slot), "value": hex(value)})
                    if (
                        job.get("discover_functions")
                        and value & 1
                        and Common.ROM_BEGIN <= value < Jp.ROM_END
                    ):
                        target = value & ~1
                        ida_segregs.split_sreg_range(
                            target, ida_idp.str2reg("T"), 1, ida_segregs.SR_user
                        )
                        ida_ua.create_insn(target)
                        ida_funcs.add_func(target)
                report.append({"job": job, "rows": rows})
            elif kind in ("disasm", "decompile", "xrefs"):
                ida_auto.auto_wait()
                ea = int(job["address"], 0)
                item = {"job": job, "name": ida_funcs.get_func_name(ea)}
                if kind == "disasm":
                    lines = []
                    for _ in range(job.get("count", 60)):
                        insn = ida_ua.insn_t()
                        size = ida_ua.decode_insn(insn, ea)
                        if not size:
                            break
                        line = ida_lines.tag_remove(ida_lines.generate_disasm_line(ea, 0) or "")
                        lines.append({"address": hex(ea), "text": line})
                        ea += size
                    item["lines"] = lines
                elif kind == "decompile":
                    try:
                        item["hexrays_initialized"] = ida_hexrays.init_hexrays_plugin()
                        failure = ida_hexrays.hexrays_failure_t()
                        result = ida_hexrays.decompile(ea, failure)
                        item["code"] = str(result) if result is not None else None
                        if result is None:
                            item["error"] = failure.desc()
                    except Exception as exc:
                        item["error"] = str(exc)
                else:
                    item["xrefs"] = [
                        {
                            "from": hex(x.frm),
                            "type": x.type,
                            "function": ida_funcs.get_func_name(x.frm),
                        }
                        for x in idautils.XrefsTo(ea)
                    ]
                report.append(item)
        ida_auto.auto_wait()
        report.append(
            {
                "processor": ida_ida.inf_get_procname(),
                "is_32bit": ida_ida.inf_is_32bit_exactly(),
                "function_count": len(list(idautils.Functions())),
            }
        )
    finally:
        idapro.close_database(True)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    import ida_idaapi

    main()
