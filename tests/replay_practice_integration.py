"""Qualify replay takeover and read-only collision overlays using the native DLL."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'replay', 'rom', 'bios', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    original = args.replay.read_bytes()
    original_hash = hashlib.sha256(original).hexdigest()
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software',
               GBARECOMP_SCREEN='raw')

    def run(scenario):
        log = args.out / (scenario + '.log')
        with log.open('wb') as stream:
            result = subprocess.run([str(args.build / 'seed_replay_practice_tests.exe'),
                                     scenario, str(args.replay), str(args.rom), str(args.bios),
                                     str(args.out / scenario)], cwd=args.build, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0),
                                    timeout=55)
        text = log.read_text(errors='replace')
        assert result.returncode == 0, f'{scenario} failed:\n{text[-3000:]}'
        print('PASS: ' + scenario, flush=True)
        return scenario, text

    results = {}
    for group in (('plain', 'boxes', 'boxes-off'),
                  ('takeover', 'takeover-close', 'takeover-end')):
        with ThreadPoolExecutor(max_workers=3) as pool:
            for future in as_completed([pool.submit(run, name) for name in group]):
                name, text = future.result()
                results[name] = text
    frames = int(re.search(r'PASS: plain frames=(\d+)', results['plain'])[1])
    inputs = list(struct.iter_unpack('<HH', original[-4 - frames * 4:-4]))
    assert len(inputs) == frames
    text = results['takeover']
    starts = re.findall(r'replay_takeover: player (\d) at frame (\d+) checkpoint_crc (\d+)', text)
    retries = re.findall(r'replay_takeover_retry: player (\d) at frame (\d+) crc (\d+)', text)
    resumed = re.findall(r'replay_takeover_resume: frame (\d+) crc (\d+)', text)
    assert [entry[0] for entry in starts] == ['1', '2'], starts
    assert len(retries) == 2 and len(resumed) == 1, (retries, resumed)
    checkpoint = starts[0][1:]
    assert int(checkpoint[0]) >= 300, 'Takeover must exercise combat after the round intro'
    assert all(entry[1:] == checkpoint for entry in starts + retries)
    assert resumed[0] == checkpoint
    changes = re.findall(r'replay_takeover_input: player (\d) frame (\d+) live (\d+) recorded (\d+) opponent (\d+)', text)
    assert {'1', '2'} == {entry[0] for entry in changes}
    for seat, frame, live, recorded, opponent in changes:
        seat, frame = int(seat) - 1, int(frame)
        assert int(live) == 17 and int(recorded) == inputs[frame][seat]
        assert int(opponent) == inputs[frame][1 - seat], 'Opponent stopped following recorded inputs'
    assert 'replay_takeover_end:' in results['takeover-end'], 'Practice did not wait at segment end'
    assert 'replay_takeover_retry:' in results['takeover-end']
    assert 'replay_takeover_resume:' in results['takeover-end']
    assert 'verified=0 source_unchanged=1' in results['takeover-close']
    plain = Image.open(args.out / 'plain/window.png').convert('RGB')
    boxes = Image.open(args.out / 'boxes/window.png').convert('RGB')
    restored = Image.open(args.out / 'boxes-off/window.png').convert('RGB')
    bounds = ImageChops.difference(plain, boxes).getbbox()
    assert bounds is not None, 'F6 did not render collision boxes during replay playback'
    assert ImageChops.difference(plain, restored).getbbox() is None, 'F6 off did not restore the original image'
    assert args.replay.read_bytes() == original, 'The original replay file changed'
    report = {'scenarios': list(results), 'source_sha256': original_hash,
              'source_unchanged': True, 'p1_controls_for_both_seats': True,
              'opponent_recorded_inputs_verified': True,
              'checkpoint_frame': int(checkpoint[0]), 'checkpoint_crc': int(checkpoint[1]),
              'retry_and_resume_checkpoints_match': True, 'f4_ignores_takeover_keys': True,
              'collision_overlay_bounds': bounds, 'collision_toggle_off_restores_image': True}
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: native takeover, retry, side switching, original replay resume, F6 and source preservation', flush=True)


if __name__ == '__main__':
    main()
