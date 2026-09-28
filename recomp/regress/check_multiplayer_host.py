#!/usr/bin/env python3
"""Run actual SDL ownership/run-loop checks in an isolated install.

Build multiplayer_probe.c as documented in regress/README.md. Uses virtual pads,
never the operator's saves, controls or log. A real window appears briefly.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe-exe', type=Path, default=ROOT / 'recomp/build/multiplayer_probe.exe')
    ap.add_argument('--fixture', type=Path, required=True,
                    help='Fresh Practice save produced by check_multiplayer_practice.py --output')
    ap.add_argument('--output', type=Path)
    ap.add_argument('--save-profile', choices=('singleplayer','multiplayer'), default='singleplayer')
    args = ap.parse_args()
    source = args.probe_exe.resolve()
    data = ROOT / 'dist/MoonstoneNative/data'
    with tempfile.TemporaryDirectory(prefix='moonstone-multiplayer-host-') as tmp:
        install = Path(tmp)
        out = args.output.resolve() if args.output else install
        out.mkdir(parents=True, exist_ok=True)
        exe = install / 'multiplayer_probe.exe'
        shutil.copyfile(source, exe)
        shutil.copyfile(source.parent / 'SDL2.dll', install / 'SDL2.dll')
        shutil.copyfile(ROOT / 'recomp/controls.ini', install / 'controls.ini')
        env = os.environ.copy()
        env['SDL_VIDEODRIVER'] = 'dummy'
        result = subprocess.run([str(exe), '--log', str(out / 'devices.log')],
                                env=env, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip(), flush=True)
        menu = out / 'menu.sav'
        base = [str(exe), '--os', '--mod', str(data / 'nb'),
                '--dataset', str(data), '--diskdir', str(data), '--save-profile', args.save_profile]
        result = subprocess.run(base + ['--probe-replay', '--frames', '8501',
                                 '--script', '7480:f,7540:.,7600:f,7660:.',
                                 '--savestate-at', '8500', str(menu),
                                 '--log', str(out / 'menu-boot.log')],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0 and menu.is_file(), result.stdout + result.stderr
        result = subprocess.run(base + ['--probe-menu', '--sdl', '--scale', '2',
                                 '--loadstate', str(menu), '--log', str(out / 'menu.log'),
                                 '--probe-image', str(out / 'menu.bmp')],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip(), flush=True)
        solo = out / 'solo.sav'
        result = subprocess.run(base + ['--probe-replay', '--loadstate', str(menu),
                                 '--frames', '1101', '--script', '0:d,100:.,200:u,210:.,260:f,320:.',
                                 '--savestate-at', '1100', str(solo),
                                 '--log', str(out / 'solo-boot.log')],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0 and solo.is_file(), result.stdout + result.stderr
        for players,pads,owner in ((1,2,1),(2,2,1),(1,1,0),(1,4,3),(1,4,0)):
            name=f'entry-{players}-{pads}pads-owner{owner+1}'
            result = subprocess.run(base + ['--probe-entry', str(players),
                                     '--probe-entry-devices',str(pads),str(owner),
                                     '--probe-menu-fixture',str(menu),'--sdl', '--scale', '2',
                                     '--loadstate', str(menu),
                                     '--log', str(out / (name+'.log')),
                                     '--probe-image', str(out / (name+'.bmp'))],
                                    capture_output=True, text=True, timeout=40)
            assert result.returncode == 0, result.stdout + result.stderr
            print(result.stdout.strip(), flush=True)
        for name, flags, fixture in (
                ('solo', ['--probe-solo'], solo),
                ('campaign', ['--probe-campaign'], ROOT / 'dist/MoonstoneNative/moonstone_edgeknight.sav'),
                ('prompts', ['--probe-prompts', '--probe-menu-fixture', str(menu)], args.fixture.resolve())):
            result = subprocess.run(base + flags + ['--sdl', '--scale', '2',
                                     '--loadstate', str(fixture), '--log', str(out / (name + '.log')),
                                     '--probe-image', str(out / (name + '.bmp'))],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            if name == 'solo':
                solo_log = (out / 'solo.log').read_text()
                assert solo_log.count('PAUSE-INJ ') == 1 and solo_log.count('PAUSE-REL ') == 1, \
                    'Solo Practice pause/resume stopped working without multiplayer setup'
            print(result.stdout.strip(), flush=True)
        result = subprocess.run([str(exe), '--probe-live', '--os', '--sdl', '--scale', '2',
                                 '--mod', str(data / 'nb'), '--dataset', str(data),
                                 '--diskdir', str(data), '--loadstate', str(args.fixture.resolve()),
                                 '--save-profile', args.save_profile,
                                 '--log', str(out / 'live.log'),
                                 '--probe-image', str(out / 'overlay.bmp')],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        log = (out / 'live.log').read_text()
        assert 'SAVESTATE ' in log and 'ok=1' in log
        assert log.count('LOADSTATE ') >= 2, 'Live F9 did not load'
        assert log.count('AQ-REPRIME ') >= 3, 'Host pause failed to re-prime audio'
        assert log.count('PAUSE-INJ ') == 1 and log.count('PAUSE-REL ') == 1, \
            'Setup/focus/reconnect input interfered with the original combat pause'
        assert (install / 'saves' / args.save_profile.title() / (args.save_profile + '.sav')).is_file(), 'Quicksave was not isolated'
        print(result.stdout.strip(), flush=True)


if __name__ == '__main__':
    main()
