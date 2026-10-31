"""Qualify the actual Windows portable installer against isolated local fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PS = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
SCRIPT = ROOT / 'launcher/Update-Portable.ps1'
VERSION = '0.1.4'
PREFIX = 'GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-' + VERSION


def digest(data):
    return hashlib.sha256(data).hexdigest()


def snapshot(root):
    return {p.relative_to(root).as_posix(): digest(p.read_bytes()) for p in root.rglob('*')
            if p.is_file() and not any(part in ('.updates','updates') for part in p.relative_to(root).parts)}


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def make_case(work, name, mutation=None, organized=False, legacy_install=False):
    folder = work / name / "portable 日本語's folder"
    folder.mkdir(parents=True)
    personal = {'launcher-settings.ini': b'fighter_outlines "0"\nus "external selected cartridge"\n',
                'controls.ini': b'custom controller mappings', 'player-name.txt': b'Kuro',
                'player-profile.ini': b'local identity', 'ms-colors.ini': b'custom palettes',
                'game-us.sav': b'player progress', 'game-jp.sav.training.state': b'practice state',
                'gamefiles/my.gba': b'private test cartridge placeholder',
                'gamefiles/gba_bios.bin': b'private test BIOS placeholder',
                'gamefiles/Game_and_Bios_Here.file': b'personal marker',
                'assets/local-preview.bin': b'local generated cache',
                'replays/a-match.seedreplay': b'personal input recording'}
    old = {'GundamSeedLauncher.exe': b'MZ old launcher', 'GundamSeedRecomp.exe': b'MZ old game',
           'SeedJpCombat.dll': b'MZ old combat', 'SDL3.dll': b'MZ old SDL',
           'controllerdb.txt': b'old controller database', 'portable-manifest.json': b'{"version":"0.1.2"}'}
    if organized and not legacy_install:
        personal = {('config/' + k if k.endswith('.ini') or k == 'player-name.txt' else k):v for k,v in personal.items()}
    for relative, data in (personal | old).items():
        target = folder / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    files = {'GundamSeedLauncher.exe': b'MZ new launcher', 'GundamSeedRecomp.exe': b'MZ new game',
             'SeedJpCombat.dll': b'MZ new combat', 'SDL3.dll': b'MZ new SDL',
             'controllerdb.txt': b'new controller database', 'launcher-settings.ini': b'fresh defaults',
             'START HERE.txt': b'new instructions', 'Update-Portable.ps1': SCRIPT.read_bytes(),
             'gamefiles/Game_and_Bios_Here.file': b'fresh marker', 'licenses/Example.txt': b'license notice'}
    manifest = {'schema': 5, 'distribution_format': 'seedpc-runtime-only-v4',
                'update_preserves_settings': True, 'version': VERSION, 'source_commit': 'a' * 40,
                'platform': 'windows-x64', 'roms_included': False, 'bios_included': False,
                'personal_data_included': False, 'game_asset_caches_included': False,
                'files': {key: {'bytes': len(value), 'sha256': digest(value)} for key, value in files.items()}}
    if organized:
        relocate = {'controllerdb.txt':'resource/controllerdb.txt','launcher-settings.ini':'config/launcher-settings.ini',
                    'Update-Portable.ps1':'config/Update-Portable.ps1','licenses/Example.txt':'resource/licenses/Example.txt'}
        files = {relocate.get(k,k):v for k,v in files.items()}
        for name in ('switch_dpad_up','switch_dpad_down','switch_dpad_left','switch_dpad_right','switch_button_a','switch_button_b','switch_button_l','switch_button_r'):
            files['resource/input-prompts/'+name+'.png'] = (ROOT / 'resources/input-prompts' / (name+'.png')).read_bytes()
        manifest['schema']=6; manifest['distribution_format']='seedpc-runtime-only-v5'
        manifest['files']={k:{'bytes':len(v),'sha256':digest(v)} for k,v in files.items()}
    if mutation:
        mutation(files, manifest)
    files['config/portable-manifest.json' if organized else 'portable-manifest.json'] = json.dumps(manifest).encode()
    job = folder / ('config/updates' if organized else '.updates') / str(uuid.uuid4())
    job.mkdir(parents=True)
    archive = job / 'release.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as package:
        for key, data in files.items():
            package.writestr(PREFIX + '/' + key, data)
    return folder, job, archive, personal, files


def invoke(root, job, archive, *, validate=False, bad_hash=False, fault=None):
    expected = '0' * 64 if bad_hash else digest(archive.read_bytes())
    args = ['-RootPath', quote(root), '-ZipPath', quote(archive), '-Hash', quote(expected),
            '-Version', quote(VERSION), '-SkipRestart']
    if validate:
        args.append('-CheckOnly')
    driver = '. ' + quote(SCRIPT) + ' -LibraryOnly\n'
    if fault == 'locked':
        driver += "$locked=[IO.File]::Open(" + quote(root / 'SeedJpCombat.dll') + ", 'Open', 'ReadWrite', 'None')\n"
    if fault == 'copy-failure':
        driver += """$script:originalReplace=(Get-Command Replace-AppFile).ScriptBlock
$script:copies=0
function Replace-AppFile([string]$Source,[string]$Target) {
 $script:copies++
 if ($script:copies -eq 3) { throw 'Controlled replacement failure' }
 & $script:originalReplace $Source $Target
}
"""
    driver += 'try { Invoke-PortableUpdate ' + ' '.join(args) + '; exit 0 } catch { Write-Output $_; exit 1 }'
    if fault == 'locked':
        driver += ' finally { $locked.Dispose() }'
    script = job / 'test-driver.ps1'
    script.write_text(driver, encoding='utf-8-sig')
    result = subprocess.run([str(PS), '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy',
                             'Bypass', '-File', str(script)], cwd=job, capture_output=True, timeout=60)
    (job / 'test-output.log').write_bytes(result.stdout + result.stderr)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    work = args.out.resolve()
    work.mkdir(parents=True, exist_ok=False)
    results = []
    cases = [
        ('success', None, {}),
        ('success-organized', None, {'organized':True}),
        ('success-legacy-to-organized', None, {'organized':True,'legacy_install':True}),
        ('organized-rollback', None, {'organized':True,'fault':'copy-failure'}),
        ('validate-only', None, {'validate': True}),
        ('bad-zip-hash', None, {'bad_hash': True}),
        ('unsafe-path', lambda files, m: files.update({'../controls.ini': b'bad'}), {}),
        ('personal-file-in-zip', lambda files, m: files.update({'controls.ini': b'bad'}), {}),
        ('bad-file-hash', lambda files, m: m['files']['SDL3.dll'].update(sha256='0' * 64), {}),
        ('game-input-policy', lambda files, m: m.update(roms_included=True), {}),
        ('case-alias', lambda files, m: files.update({'sdl3.dll': b'alias'}), {}),
        ('locked-runtime', None, {'fault': 'locked'}),
        ('failed-copy-rollback', None, {'fault': 'copy-failure'}),
    ]
    for name, mutation, options in cases:
        organized=options.pop('organized',False); legacy_install=options.pop('legacy_install',False)
        root, job, archive, personal, files = make_case(work, name, mutation, organized, legacy_install)
        before = snapshot(root)
        code = invoke(root, job, archive, **options)
        should_succeed = name.startswith('success') or name == 'validate-only'
        assert (code == 0) == should_succeed, (name, code, str(job / 'test-output.log'))
        after = snapshot(root)
        if name.startswith('success'):
            for relative, data in personal.items():
                assert after[relative] == digest(data), ('personal file changed', relative)
            replaced = ('GundamSeedLauncher.exe','GundamSeedRecomp.exe','SeedJpCombat.dll','SDL3.dll') + (('resource/controllerdb.txt','config/portable-manifest.json','config/Update-Portable.ps1') if organized else ('controllerdb.txt','portable-manifest.json','Update-Portable.ps1'))
            for relative in replaced:
                assert after[relative] == digest(files[relative]), ('runtime not replaced', relative)
            assert json.loads((job / 'journal.json').read_text())['status'] == 'completed'
        else:
            assert after == before, (name, 'installation or personal files changed')
            if name in ('failed-copy-rollback','organized-rollback'):
                assert json.loads((job / 'journal.json').read_text())['status'] == 'rolled-back'
        results.append({'case': name, 'exit_code': code, 'personal_files_preserved': True})
        print('PASS:', name, flush=True)
    (work / 'report.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
