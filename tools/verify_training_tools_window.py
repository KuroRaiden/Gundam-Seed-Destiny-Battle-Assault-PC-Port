"""Exercise Training quick states in the real SDL event/present loop with scratch files."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
from verify_jp_local_vs import Client
from game_addresses import Jp

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'bios', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--port', type=int, default=24982)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    save = args.out / 'window.sav'
    evidence = []
    for run in range(2):
        log = (args.out / f'run-{run}.log').open('w')
        process = subprocess.Popen([str(args.exe.resolve()), '--training', '--rom', str(args.rom.resolve()), '--bios', str(args.bios.resolve()), '--bios-hle', '--save', str(save), '--window', '--scale', '3', '--tcp-observe', str(args.port)], cwd=args.out, stdout=log, stderr=log, env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', GBARECOMP_SELFHEAL_RECOMPILE='0'))
        client = None

        def wait(predicate, label):
            until = time.monotonic() + 20
            while time.monotonic() < until:
                if predicate():
                    return
                if process.poll() is not None:
                    raise RuntimeError('Game exited: ' + label)
                time.sleep(0.02)
            raise RuntimeError('Timed out: ' + label + ' ' + str(client.status()) + ' ' + str(client.cmd('run_status')))
        try:
            client = Client(args.port, process)
            wait(lambda: client.status()['substate'] == 2 and client.status()['round'] == 2, 'Training ready')

            def x():
                data = client.cmd('read_iwram', addr=Jp.BATTLE_CONTEXT + Jp.FIGHTER_RECORDS_OFFSET + 36, len=4)['data']
                return int.from_bytes(bytes.fromhex(data), 'little', signed=True) / 65536

            def state(action):
                client.cmd('seed_training_tools', action=action)
                wait(lambda: not client.cmd('seed_training_tools')['pending'], 'checkpoint ' + action)
            client.cmd('seed_input', p1=0, p2=0)
            if run == 0:
                client.cmd('seed_training_tools', action='boxes')
                assert client.cmd('seed_training_tools')['boxes_visible']
                state('save')
                saved_x = x()
                state_file = save.with_name(save.name + '.training.state')
                assert state_file.is_file()
                (args.out / 'saved-x.txt').write_text(str(saved_x))
                client.cmd('seed_input', p1=16, p2=0)
                time.sleep(0.7)
                client.cmd('seed_input', p1=0, p2=0)
                assert x() > saved_x + 5
            else:
                saved_x = float((args.out / 'saved-x.txt').read_text())
            before = client.cmd('run_status')['frame']
            state('load')
            after = client.cmd('run_status')['frame']
            assert abs(x() - saved_x) < 2, (saved_x, x())
            if run == 0:
                assert after < before - 10, (before, after)
            prior = after
            time.sleep(0.3)
            assert client.cmd('run_status')['frame'] > prior + 5, 'Restored game stopped advancing'

            def start(expected):
                client.cmd('seed_input', p1=0, p2=0)
                wait(lambda: client.status()['raw'][0] == 0, 'native neutral input')
                time.sleep(0.08)
                client.cmd('seed_input', p1=8, p2=0)
                wait(lambda: client.status()['raw'][0] == 8, 'native Start input')
                wait(lambda: client.status()['substate'] == expected, 'native pause/resume')
                client.cmd('seed_input', p1=0, p2=0)
                wait(lambda: client.status()['raw'][0] == 0, 'native Start release')
            for cycle in range(3):
                start(4)
                state('save')
                state('load')
                assert client.status()['substate'] == 4, 'Paused checkpoint lost menu state'
                client.cmd('seed_input', p1=0, p2=0)
                time.sleep(0.12)
                start(2)
                print('PASS pause/load/resume cycle', run, cycle, flush=True)
            state('save')
            evidence.append(dict(run=run, before_load=before, after_load=after, saved_x=saved_x, restored_x=x(), paused_cycles=3, status=client.status()))
            print('PASS live SDL Training state run', run, flush=True)
        finally:
            if client:
                client.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            log.close()
    (args.out / 'report.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('PASS: live window unwind/load, continued frames, paused states, resume and persistence across process restarts.')
if __name__ == '__main__':
    main()
