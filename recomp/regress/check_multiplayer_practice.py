#!/usr/bin/env python3
"""Replay both independent inputs through the original Practice combat code.

Uses local game disks read-only and puts all saves, RAM dumps and logs in scratch.
The opt-in --script2 channel uses exactly the live original joystick-selector hook.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from check_practice_input import BLUE, GREEN, BOOT_SCRIPT, ROOT, saved_ram, word, pose


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data = ROOT / 'dist/MoonstoneNative/data'
    base = [str(args.exe.resolve()), '--os', '--mod', str(data / 'nb'),
            '--dataset', str(data), '--diskdir', str(data)]
    with tempfile.TemporaryDirectory(prefix='moonstone-multiplayer-') as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)

        def invoke(name, extra):
            result = subprocess.run(base + ['--log', str(out / (name + '.log'))] + extra,
                                    capture_output=True, text=True, timeout=90)
            assert result.returncode == 0, f'{name}: {result.stdout}\n{result.stderr}'

        fixture = out / 'practice.sav'
        # Leave the campaign at its default Players 1: Practice has two players.
        invoke('boot', ['--frames', '10001', '--script', BOOT_SCRIPT, '--script2', '0:.',
                        '--savestate-at', '10000', str(fixture)])
        start = saved_ram(fixture)
        assert word(start, 0x3051E) == 2
        assert word(start, 0x2E064) == 1, 'Test must enter Practice with campaign Players 1'
        assert word(start, 0x2E024) == 2, 'Original Practice must set up two players'
        assert start[BLUE + 11] == 2 and start[GREEN + 11] == 1
        assert 'MP-PRACTICE original setup complete' in (out / 'boot.log').read_text()

        def run(name, p1='0:.', p2='0:.', extra=(), source=fixture, frames=250, snapshot=60):
            mid, end = out / (name + '.sav'), out / (name + '.bin')
            invoke(name, ['--loadstate', str(source), '--frames', str(frames),
                          '--script', p1, '--script2', p2,
                          '--savestate-at', str(snapshot), str(mid), '--dumpram', str(end), *extra])
            return saved_ram(mid), end.read_bytes(), mid

        idle, idle_end, _ = run('idle')
        for player, actor, other in ((0, BLUE, GREEN), (1, GREEN, BLUE)):
            for keys in ('l', 'r', 'u', 'd', 'f'):
                scripts = ['0:.', '0:.']
                scripts[player] = f'0:{keys},70:.'
                mid, end, _ = run(f'p{player+1}-{keys}', *scripts)
                assert pose(mid, other) == pose(idle, other), f'P{player+1} {keys}: moved other knight'
                assert word(mid, other + 0x3E) == 0, 'Input leaked to other knight'
                expected = {'r': 1, 'l': 2, 'd': 4, 'u': 8, 'f': 16}[keys]
                # The original movement routine clears a direction at arena edges.
                assert word(mid, actor + 0x3E) in (0, expected), f'P{player+1} {keys}: wrong input'
                if keys != 'f':
                    assert pose(mid, actor) != pose(idle, actor), 'Assigned knight did not move'
                else:
                    assert word(mid, actor + 0x3E) == 16, 'Assigned knight did not attack'
                assert word(end, actor + 0x3E) == 0, 'Held input survived release'
        print('PASS: both original knights move and attack independently; release clears input', flush=True)

        mid, end, checkpoint = run('simultaneous', '0:l,70:.', '0:r,70:.', snapshot=15)
        assert word(mid, BLUE + 0x3E) == 2 and word(mid, GREEN + 0x3E) == 1
        assert pose(mid, BLUE) != pose(idle, BLUE) and pose(mid, GREEN) != pose(idle, GREEN)
        released, released_end, _ = run('reload', source=checkpoint)
        assert word(released, BLUE + 0x3E) == word(released, GREEN + 0x3E) == 0
        assert pose(released, BLUE) == pose(released_end, BLUE)
        assert pose(released, GREEN) == pose(released_end, GREEN)
        print('PASS: simultaneous input and cold save/reload use both original control ports', flush=True)

        off, _, _ = run('without-parity', '0:l,70:.', '0:r,70:.', ['--noretailparity'], snapshot=15)
        assert pose(off, BLUE) == pose(mid, BLUE) and pose(off, GREEN) == pose(mid, GREEN)
        print('PASS: independent inputs also work with retail-parity patches disabled', flush=True)

        hit = '0:u,34:.,45:l,115:.,140:lf,180:.,200:lf,240:.,260:lf,300:.'
        _, p1_hit, _ = run('p1-hit', hit, frames=360)
        assert word(p1_hit, GREEN + 0x50) < word(start, GREEN + 0x50)
        # Green starts to the left and above blue; approach right/down instead.
        hit2 = '0:d,34:.,45:r,115:.,140:rf,180:.,200:rf,240:.,260:rf,300:.'
        _, p2_hit, _ = run('p2-hit', p2=hit2, frames=360)
        assert word(p2_hit, BLUE + 0x50) < word(start, BLUE + 0x50), 'P2 attacks cause no damage'
        print('PASS: both players cause damage through the original combat code', flush=True)


if __name__ == '__main__':
    main()
