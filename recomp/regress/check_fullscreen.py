"""Exercise fullscreen in the real SDL loop, using isolated logs/saves/configs.

Requires fullscreen_probe.exe built with the normal build.sh compiler inputs.
Windows will briefly show the test window and switch it into fullscreen.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe-exe', type=Path, required=True)
    ap.add_argument('--menu', type=Path, required=True)
    ap.add_argument('--practice', type=Path, required=True)
    ap.add_argument('--map', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--case', action='append', help='Run only the named scenario (repeatable)')
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    cases = [('control', args.menu), ('window', args.menu), ('maximized', args.menu),
             ('focus', args.practice), ('failure', args.menu), ('remap', args.menu),
             ('reserved', args.menu), ('map', args.map), ('combat', args.practice),
             ('prompt', args.practice), ('reconnect', args.practice), ('intro', None)]
    if args.case:
        assert set(args.case) <= {name for name, _ in cases}
        cases = [(name, fixture) for name, fixture in cases if name in args.case or name == 'control']
    with tempfile.TemporaryDirectory(prefix='moonstone-fullscreen-') as tmp:
        install = Path(tmp)
        exe = install / 'fullscreen_probe.exe'
        shutil.copyfile(args.probe_exe.resolve(), exe)
        shutil.copyfile(args.probe_exe.resolve().parent / 'SDL2.dll', install / 'SDL2.dll')
        controls = (ROOT / 'recomp/controls.ini').read_text()
        baseline = None
        for name, fixture in cases:
            folder = out / name
            folder.mkdir(exist_ok=True)
            config = controls
            if name == 'remap':
                config = config.replace('quit = Escape', 'quit = F8').replace('up = Up', 'up = Escape')
                config = config.replace('attack_select = Left Ctrl, Right Ctrl, Return, Keypad Enter', 'attack_select = Escape')
            if name == 'reserved':
                config = config.replace('attack_select = Left Ctrl, Right Ctrl, Return, Keypad Enter', 'attack_select = F11')
            (install / 'controls.ini').write_text(config)
            cmd = [str(exe), '--os', '--sdl', '--scale', '2', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--log', str(folder / 'game.log'),
                   '--probe-output', str(folder), '--probe-case', name]
            if fixture:
                cmd += ['--loadstate', str(fixture.resolve())]
            r = subprocess.run(cmd, cwd=install, capture_output=True, text=True, timeout=35,
                               creationflags=0x08000000 if os.name == 'nt' else 0)
            (folder / 'stdout.txt').write_text(r.stdout + r.stderr)
            assert r.returncode == 0, (name, r.stdout, r.stderr)
            log = (folder / 'game.log').read_text()
            assert log.count('result=failed') == (2 if name == 'failure' else 0)
            assert 'CONTROLS-WARN' not in log or name == 'reserved'
            state = (folder / 'final.sav').read_bytes()[104:104+0x200000]
            if name == 'control':
                baseline = state
            elif fixture == args.menu:
                assert state == baseline, f'{name}: changed guest RAM'
            print(r.stdout.strip(), flush=True)
        if any(name == 'window' for name, _ in cases):
            assert hashlib.sha256((out / 'window/windowed.bmp').read_bytes()).digest() == \
                   hashlib.sha256((out / 'control/windowed.bmp').read_bytes()).digest(), \
                   'Returning to windowed mode changed the rendered game image'
        print(f'PASS: {len(cases)} fullscreen scenarios; all menu runs preserve full guest RAM', flush=True)


if __name__ == '__main__':
    main()
