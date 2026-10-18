#!/usr/bin/env python3
"""Compare the actual boot texture with its source PNG and an optional old build.

Build splash_probe.c using build.py --probe, never package the probe. The optional
baseline probe must be compiled before changing the splash loading code. Uses
Pillow, already required by the asset generator; installs nothing. All game
output goes to a new scratch directory with an explicit absolute log path.
"""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def header_pixels(text):
    """Independently decode historical headers as well as current raw pixels."""
    for name in ('splash_rgb', 'splash_qoi'):
        marker = name + '[] = {'
        if marker in text:
            body = text.split(marker, 1)[1].split('};', 1)[0]
            stored = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', body))
            if name == 'splash_rgb':
                return stored, stored, 'RGB24'
            break
    else:
        encoded = ''.join(re.findall(r'^"([A-Za-z0-9+/=]+)"', text, re.M))
        if not encoded:
            raise ValueError('Unrecognized splash header')
        stored = base64.b64decode(encoded, validate=True)
    with Image.open(io.BytesIO(stored)) as im:
        assert im.format == 'QOI' and im.size == (960, 768)
        return stored, im.convert('RGB').tobytes(), 'QOI'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe', required=True, type=Path)
    ap.add_argument('--baseline-probe', type=Path)
    ap.add_argument('--baseline-header', type=Path)
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    text = (ROOT / 'recomp/src/splash_image.h').read_text()
    stored, pixels, storage = header_pixels(text)
    if args.baseline_header:
        original = args.baseline_header.read_text()
        assert pixels == header_pixels(original)[1], 'Original image pixels changed'
    expected = Image.open(ROOT / 'recomp/icon/splash.png').convert('RGB').resize(
        (960, 768), Image.Resampling.LANCZOS).tobytes()
    assert pixels == expected, 'Embedded pixels differ from source PNG'
    probes = [('candidate', args.probe.resolve())]
    if args.baseline_probe:
        probes.insert(0, ('baseline', args.baseline_probe.resolve()))
    reference = None
    runs = []
    for name, exe in probes:
        folder = out / name
        folder.mkdir()
        shutil.copyfile(ROOT / 'recomp/controls.ini', folder / 'controls.ini')
        data = ROOT / 'dist/MoonstoneNative/data'
        result = subprocess.run(list(map(str, [exe, '--os', '--sdl', '--scale', '1',
            '--dataset', data, '--diskdir', data, '--log', folder / 'game.log',
            '--probe-output', folder])), cwd=folder, capture_output=True, text=True,
            timeout=30, creationflags=0x08000000)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        assert (folder / 'splash.rgb').read_bytes() == expected, 'Decoded pixels changed'
        pictures = [(folder / f'{mode}.bmp').read_bytes()
                    for mode in ('windowed', 'fullscreen', 'restored')]
        assert pictures[0] == pictures[2], 'Returning to windowed mode changed pixels'
        if reference is not None:
            assert pictures == reference, 'Rendered splash changed'
        reference = pictures
        assert not list(folder.rglob('*.sav'))
        runs.append(dict(name=name, exe=str(exe),
                         pictures=[hashlib.sha256(p).hexdigest() for p in pictures]))
        print(f'PASS {name}: exact source pixels; real windowed/fullscreen/restored renders', flush=True)
    report = dict(storage=storage, stored_bytes=len(stored), stored_sha256=hashlib.sha256(stored).hexdigest(),
                  rgb_bytes=len(expected), rgb_sha256=hashlib.sha256(expected).hexdigest(), runs=runs)
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
