"""Verify cold-cache preparation, native boot, and a cache-preserving warm launch."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from verify_jp_local_vs import Client

ASSETS = ("destiny-selection.bin", "destiny-stages.bin", "us-training-menu.bin")
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("exe","rom","jp-rom","bios","out"):
        parser.add_argument("--"+name,type=Path,required=True)
    parser.add_argument("--port",type=int,default=25460)
    parser.add_argument("--baseline",action="store_true",help="Use the original direct first-launch path")
    args=parser.parse_args()
    folder=args.out.resolve()
    if folder.exists():raise RuntimeError("Use a fresh E: test directory")
    folder.mkdir(parents=True)
    for name in ("GundamSeedRecomp.exe","SeedJpCombat.dll","SDL3.dll"):
        shutil.copy2(args.exe.resolve().parent/name,folder/name)
    shutil.copytree(args.exe.resolve().parent/"resource",folder/"resource")
    (folder/"Logs").mkdir()
    env=dict(os.environ,GBARECOMP_SELFHEAL_RECOMPILE="0",SDL_VIDEODRIVER="dummy",SDL_RENDER_DRIVER="software")
    command=[str(folder/"GundamSeedRecomp.exe"),"--rom",str(args.rom.resolve()),"--jp-rom",str(args.jp_rom.resolve()),
        "--bios",str(args.bios.resolve()),"--bios-hle","--save",str(folder/"test.sav")]
    records=[]
    for run in ("cold","warm"):
        before={name:(folder/"assets"/name).stat().st_mtime_ns for name in ASSETS if (folder/"assets"/name).exists()}
        began=time.monotonic()
        if run=="cold" and not args.baseline:
            with (folder/"Logs/first-launch.log").open("w",encoding="utf-8") as log:
                result=subprocess.run(command+["--prepare-game-assets"],cwd=folder,env=env,stdout=log,stderr=log,timeout=180)
            assert result.returncode==0, f"Preparation failed: {result.returncode}"
            assert all((folder/"assets"/name).is_file() for name in ASSETS)
        with (folder/"Logs"/(run+".log")).open("w",encoding="utf-8") as log:
            process=subprocess.Popen(command+["--tcp",str(args.port)],cwd=folder,env=env,stdout=log,stderr=log)
            client=None
            try:
                client=Client(args.port,process,startup_timeout=180)
                state=client.status()
                client.frames(10)
                client.cmd("quit")
                process.wait(timeout=15)
                assert process.returncode==0,process.returncode
                assert all((folder/"assets"/name).is_file() for name in ASSETS)
                if run=="warm":
                    assert all((folder/"assets"/name).stat().st_mtime_ns==before[name] for name in ASSETS)
                records.append({"run":run,"exit_code":process.returncode,"native_state":state["state"],
                    "seconds":round(time.monotonic()-began,3),"assets_ready":True,"cache_reused":run=="warm"})
            finally:
                if client:client.close()
                if process.poll() is None:process.terminate();process.wait(timeout=10)
    (folder/"report.json").write_text(json.dumps(records,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(records),flush=True)
    print("PASS: cold and warm native startup, generated assets, clean exit and warm-cache reuse.")
if __name__=="__main__":main()
