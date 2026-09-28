"""Real F5/F9, isolated failures, portable launchers and explicit-path checks.

Build save_profile_probe.c and both launchers first (see regress/README.md).
All runtime files, saves and absolute logs are private copies below --output.
Refuses to reuse output directories so baseline saves cannot be overwritten.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT/'dist/MoonstoneNative/data'
BUILD = ROOT/'recomp/build'
NAMES = ('singleplayer.sav', 'multiplayer.sav', 'moonstone.sav')
FLAGS = 0x08000000 if os.name == 'nt' else 0

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def slot(install, profile):
    return install/'saves'/profile.title()/(profile+'.sav')

def run(command, cwd):
    result = subprocess.run(list(map(str, command)), cwd=cwd, capture_output=True,
                            text=True, timeout=40, creationflags=FLAGS)
    assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--other-fixture', type=Path, required=True,
                    help='A copied multiplayer save; verifies profile is host-only')
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    other = args.other_fixture.resolve()
    solo = ROOT/'dist/MoonstoneNative/moonstone_edgeknight.sav'
    unrelated = out/'Unrelated working directory'
    unrelated.mkdir()
    results = []
    for profile in ('default', 'singleplayer', 'multiplayer'):
        selected = 'singleplayer' if profile == 'default' else profile
        for mode in ('roundtrip', 'renamed', 'missing', 'corrupt', 'write-failure',
                     'fresh', 'missing-folders', 'parent-only', 'blocked-parent',
                     'blocked-profile', 'denied-parent'):
            case = out/(profile+'-'+mode)
            case.mkdir()
            exe = case/'moonstone.exe'
            shutil.copyfile(BUILD/'save_profile_probe.exe', exe)
            shutil.copyfile(BUILD/'SDL2.dll', case/'SDL2.dll')
            shutil.copyfile(ROOT/'recomp/controls.ini', case/'controls.ini')
            for name in NAMES: shutil.copyfile(other, case/name)
            target = slot(case,selected)
            if mode in ('roundtrip','renamed','missing','corrupt','write-failure'):
                for choice in ('singleplayer','multiplayer'):
                    path=slot(case,choice);path.parent.mkdir(parents=True)
                    shutil.copyfile(other,path)
            if mode in ('parent-only','blocked-profile','denied-parent'):
                (case/'saves').mkdir()
            blocker=None
            if mode=='blocked-parent': blocker=case/'saves'
            if mode=='blocked-profile': blocker=target.parent
            if blocker: blocker.write_bytes(b'Existing file must not be replaced')
            if mode == 'missing': target.unlink()
            if mode == 'corrupt': target.write_bytes(b'MOONSAVE-broken')
            before = {p:sha(p) for p in case.rglob('*.sav')}
            if blocker: before[blocker]=sha(blocker)
            probe_mode = ('roundtrip' if mode in ('fresh','parent-only') else
                          'missing' if mode=='missing-folders' else
                          'folder-failure' if mode in ('blocked-parent','blocked-profile','denied-parent') else mode)
            result = case/'result.txt'
            cmd = [exe,'--os','--sdl','--scale','1','--dataset',DATA,'--diskdir',DATA,
                   '--loadstate',solo,'--log',case/'game.log','--probe-mode',probe_mode,
                   '--probe-profile',selected,'--probe-result',result]
            if profile != 'default': cmd += ['--save-profile',profile]
            if probe_mode == 'roundtrip': cmd += ['--probe-other',other]
            # Deny creation of subdirectories only on this new private saves
            # parent. No inherited ACE: reads/logging remain permitted.
            denied=case/'saves' if mode=='denied-parent' else None
            if denied:
                assert denied.resolve().is_relative_to(out) and denied.is_dir()
                subprocess.run(['icacls',str(denied),'/deny','*S-1-1-0:(AD)'],check=True,capture_output=True)
            try:
                run(cmd, unrelated)
            finally:
                if denied:
                    subprocess.run(['icacls',str(denied),'/remove:d','*S-1-1-0'],check=True,capture_output=True)
            assert result.read_text().startswith('PASS ')
            assert all(sha(p)==h for p,h in before.items()
                       if p!=target or probe_mode!='roundtrip'), (profile,mode)
            if mode in ('missing','missing-folders'): assert not target.exists()
            if mode=='missing-folders': assert not (case/'saves').exists()
            if probe_mode == 'roundtrip':
                if target in before: assert sha(target) != before[target]
                assert struct.unpack_from('<I',target.read_bytes(),8)[0] == 5
            assert not list(case.rglob('*.tmp.*'))
            log = (case/'game.log').read_text()
            assert f'SAVE-PROFILE {selected} quicksave={target.as_posix()}' in log.replace('\\','/')
            if probe_mode=='folder-failure':
                assert 'QUICKSAVE cannot create folder' in log and not target.exists()
            if denied: assert 'Windows error 5:' in log
            assert not list(unrelated.rglob('*.sav'))
            results.append(f'{profile}/{mode}: '+result.read_text().strip())
            print(results[-1],flush=True)

    # Real portable launch entries, relocated after assembly. The child is the
    # observer build of the actual SDL engine and exits after exercising F5/F9.
    package = out/'Assembled game'
    package.mkdir()
    for name in ('Moonstone Singleplayer.exe','Moonstone Multiplayer.exe','SDL2.dll'):
        shutil.copyfile(BUILD/name,package/name)
    shutil.copyfile(BUILD/'save_profile_probe.exe',package/'moonstone.exe')
    shutil.copyfile(ROOT/'recomp/controls.ini',package/'controls.ini')
    moved = out/'Moved Game with spaces'
    package.rename(moved)
    for selected in ('singleplayer','multiplayer'):
        result = moved/(selected+'-result.txt')
        log = moved/(selected+'.log')
        run([moved/f'Moonstone {selected.title()}.exe','--scale','1','--dataset',DATA,
             '--diskdir',DATA,'--loadstate',solo,'--log',log,'--probe-mode','roundtrip',
             '--probe-profile',selected,'--probe-result',result],unrelated)
        deadline = time.monotonic()+30
        while not result.exists() and time.monotonic()<deadline: time.sleep(0.1)
        assert result.exists() and result.read_text().startswith('PASS '), log
        assert slot(moved,selected).is_file()
        assert not (moved/(selected+'.sav')).exists()
        assert not (moved/'moonstone.sav').exists()
        results.append('PASS relocated '+selected+' launcher: quoted paths, unrelated CWD, SDL F5/F9')
        print(results[-1],flush=True)

    # Invalid selectors must not reach game-data extraction or modify a slot.
    for index, flags in enumerate((['--save-profile'],['--save-profile','typo'],
                                   ['--save-profile','--frames','1'])):
        log = out/f'invalid-{index}.log'
        result = subprocess.run([str(BUILD/'moonstone.exe'),*flags,'--log',str(log)],
                                cwd=unrelated,capture_output=True,text=True,timeout=10,creationflags=FLAGS)
        assert result.returncode == 2, result.stdout+result.stderr
        assert 'Missing or invalid save profile' in log.read_text()
        assert 'LINEAGE:' not in log.read_text()
    print('PASS 3 invalid/missing selectors rejected before data loading',flush=True)

    # Diagnostic paths still replace only the explicitly requested target;
    # selected save profile has no effect on RAM, audio or v5 state bytes.
    reference = None
    for selected in ('singleplayer','multiplayer'):
        folder = out/('explicit-'+selected); folder.mkdir()
        run([BUILD/'moonstone.exe','--os','--dataset',DATA,'--diskdir',DATA,
             '--save-profile',selected,'--loadstate-at','0',solo,'--frames','3',
             '--savestate-at','2',folder/'diagnostic.sav','--dumpram',folder/'ram.bin',
             '--wav',folder/'audio.wav','--log',folder/'game.log'],unrelated)
        got = [sha(folder/name) for name in ('diagnostic.sav','ram.bin','audio.wav')]
        if reference is None: reference = got
        assert got == reference
        results.append('PASS explicit diagnostic paths '+selected+': identical save/RAM/audio')
        print(results[-1],flush=True)
    for name in ('moonstone.exe','Moonstone Singleplayer.exe','Moonstone Multiplayer.exe'):
        binary = (BUILD/name).read_bytes()
        pe = struct.unpack_from('<I',binary,0x3c)[0]
        assert struct.unpack_from('<H',binary,pe+24+68)[0] == 2, name
    print('PASS all three production executables use Windows GUI subsystem',flush=True)
    (out/'results.json').write_text(json.dumps(dict(cases=results,invalid_profiles=3,
        gui_executables=3,save_format=5),indent=2)+'\n')

if __name__ == '__main__': main()
