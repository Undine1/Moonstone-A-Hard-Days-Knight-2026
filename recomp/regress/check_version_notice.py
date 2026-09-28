"""Check the actual SDL version toggle without touching the player's install.

Requires version_notice_probe.exe built with the normal compiler inputs.
The supplied fixture must be a single-player campaign awaiting map input.
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
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    source = args.probe_exe.resolve()
    with tempfile.TemporaryDirectory(prefix='moonstone-version-') as tmp:
        install = Path(tmp)
        exe = install / 'version_notice_probe.exe'
        shutil.copyfile(source, exe)
        shutil.copyfile(source.parent / 'SDL2.dll', install / 'SDL2.dll')
        controls = (ROOT / 'recomp/controls.ini').read_text()
        results = {}
        for name, flags in [('control', ['--probe-control']), ('keyboard', []),
                            ('remap', ['--probe-remap']), ('controller', ['--probe-controller'])]:
            folder = output / name
            folder.mkdir(exist_ok=True)
            config = controls.replace('show_version = V', 'show_version = B') if name == 'remap' else controls
            config = config.replace('show_version = none', 'show_version = X')
            (install / 'controls.ini').write_text(config)
            cmd = [str(exe), '--os', '--sdl', '--scale', '2', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(args.fixture.resolve()),
                   '--log', str(folder / 'game.log'), '--wav', str(folder / 'audio.wav'),
                   '--probe-output', str(folder)] + flags
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=40,
                               creationflags=0x08000000 if os.name == 'nt' else 0)
            (folder / 'stdout.txt').write_text(r.stdout + r.stderr)
            assert r.returncode == 0, (name, r.stdout, r.stderr)
            log = (folder / 'game.log').read_text()
            assert 'V-VERSION alert' not in log and 'VERSION recovered' not in log
            assert log.count('VERSION shown: v1.4 - 2026 revision - Undine') == (0 if name == 'control' else 3)
            assert log.count('VERSION hidden:') == (0 if name == 'control' else 1)
            assert 'SAVESTATE ' in log and log.count('LOADSTATE ') >= 2
            state = (folder / 'final.sav').read_bytes()
            results[name] = (state[104:104+0x200000], (folder / 'audio.wav').read_bytes())
            assert results[name] == results['control'], (name, 'toggle changed guest RAM or audio')
            print('PASS', name, ': SDL checks; full RAM/PCM match the no-version control', flush=True)
        sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        assert sha(output / 'keyboard/shown.bmp') != sha(output / 'control/shown.bmp')
        for image in ('hidden.bmp', 'inventory.bmp'):
            assert sha(output / 'keyboard' / image) == sha(output / 'control' / image), image
        print('PASS overlay pixels appear and disappear without altering the game image', flush=True)


if __name__ == '__main__':
    main()
