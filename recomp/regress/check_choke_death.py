#!/usr/bin/env python3
"""Replay a frozen canopy choke through death, attack attempts, and save/reload.

Uses the untouched August live specimen (18 player HP, 5 monkey HP). All logs,
captures, and new saves go to a temporary directory or --output scratch folder.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PLAYER, MONKEY = 0x2E7DC, 0x134692


def word(ram, addr):
    return struct.unpack_from('>h', ram, addr)[0]


def saved_ram(path):
    data = path.read_bytes()
    start = 20 + struct.unpack_from('<I', data, 16)[0] * 4
    return data[start:start + 0x200000]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--output', type=Path, help='Keep results here for visual review')
    args = ap.parse_args()
    exe = args.exe.resolve()
    data = ROOT / 'dist/MoonstoneNative/data'
    fixture = ROOT / 'dist/MoonstoneNative/choke-stab-live-2026-08-09.sav'
    source = saved_ram(fixture)
    assert word(source, PLAYER + 0x50) == 18 and word(source, MONKEY + 0x50) == 5

    with tempfile.TemporaryDirectory(prefix='moonstone-choke-') as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)

        def run(name, script, extra=(), save=fixture, frames=600, snapshot=220):
            case = out / name
            case.mkdir(exist_ok=True)
            cmd = [str(exe), '--os', '--mod', str(data / 'nb'), '--dataset', str(data),
                   '--diskdir', str(data), '--loadstate', str(save), '--frames', str(frames),
                   '--script', script, '--log', str(case / 'run.log'),
                   '--savestate-at', str(snapshot), str(case / 'mid.sav'),
                   '--dumpram', str(case / 'ram.bin'), '--dump', str(case / 'frame'),
                   '--dumpevery', '20', *extra]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, f'{name}: {result.stderr}'
            return case, saved_ram(case / 'mid.sav'), (case / 'ram.bin').read_bytes()

        # HP reaches zero naturally at frame 172. Attack is first pressed AFTER death.
        script = '0:.,180:df,300:.'
        old, old_mid, _ = run('disabled', script, ['--nochokedeathfix'])
        assert word(old_mid, PLAYER + 0x50) <= 0 and word(old_mid, MONKEY + 0x50) <= 0, \
            'The comparison must reproduce a dead player attacking the monkey'
        print('PASS: disabled fix reproduces attack after death', flush=True)

        idle, idle_mid, idle_end = run('idle', '0:.')
        fixed, mid, end = run('late-attack', script)
        for label, state, final in [('idle', idle_mid, idle_end), ('late-attack', mid, end)]:
            assert word(state, PLAYER + 0x50) == 0, f'{label}: HP must stop draining at death'
            assert word(state, MONKEY + 0x50) == 5, f'{label}: dead player dealt damage'
            assert final[PLAYER + 0x49] == source[PLAYER + 0x49] - 1, f'{label}: wrong life loss'
            assert word(final, PLAYER + 0x50) == word(source, PLAYER + 0x54), f'{label}: HP not restored'
        for frame in (200, 220, 240, 260, 280):
            image = f'frame_{frame:04d}.ppm'
            assert (fixed / image).read_bytes() == (idle / image).read_bytes(), \
                f'Attack changed the death display at frame {frame}'
        assert (fixed / 'frame_0200.ppm').read_bytes() != (old / 'frame_0200.ppm').read_bytes()
        assert (fixed / 'frame_0580.ppm').read_bytes() == (idle / 'frame_0580.ppm').read_bytes(), \
            'Post-death inventory display differs after attack attempts'
        end_frames = [re.search(r'DAYEND-WRITE .* fr=(\d+)', (case / 'run.log').read_text()).group(1)
                      for case in (old, idle, fixed)]
        assert len(set(end_frames)) == 1, 'Death-to-inventory timing changed'
        print('PASS: death stays visible; attack is ignored; exactly one life is lost', flush=True)

        _, resumed, resumed_end = run('reloaded-death', '0:df,80:.', save=fixed / 'mid.sav', frames=400, snapshot=40)
        assert word(resumed, PLAYER + 0x50) == 0 and word(resumed, MONKEY + 0x50) == 5
        assert resumed_end[PLAYER + 0x49] == source[PLAYER + 0x49] - 1
        assert word(resumed_end, PLAYER + 0x50) == word(source, PLAYER + 0x54)
        print('PASS: save/reload during death preserves the outcome', flush=True)

        # Still-alive escape must retain the previously fixed upward stab/release.
        early, _, early_end = run('living-escape', '0:df,80:.')
        early_old, _, early_old_end = run('living-escape-disabled', '0:df,80:.', ['--nochokedeathfix'])
        assert early_end == early_old_end, 'Living escape changed guest RAM'
        assert early_end[PLAYER + 0x49] == source[PLAYER + 0x49]
        for frame in (0, 20, 40, 60):
            image = f'frame_{frame:04d}.ppm'
            assert (early / image).read_bytes() == (early_old / image).read_bytes()
        print('PASS: living escape animation and outcome remain identical', flush=True)

        _, raw_mid, raw_end = run('without-retail-parity', script, ['--noretailparity'])
        assert word(raw_mid, PLAYER + 0x50) == 0 and word(raw_mid, MONKEY + 0x50) == 5
        assert raw_end[PLAYER + 0x49] == source[PLAYER + 0x49] - 1
        print('PASS: death handling also works without retail parity', flush=True)
        if args.output:
            print(f'Visual review: {out}')


if __name__ == '__main__':
    main()
