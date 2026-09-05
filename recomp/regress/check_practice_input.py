#!/usr/bin/env python3
"""Check that Practice's second human knight ignores player-one/menu input.

Boots from the operator's local disks; creates all saves and logs in scratch.
No copyrighted fixture is required or modified. The disabled-fix comparisons
must reproduce both wandering after release and a mirrored attack.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BLUE, GREEN = 0x2E7DC, 0x2E860
BOOT_SCRIPT = '7480:f,7540:.,7600:f,7660:.,9000:d,9100:.,9200:u,9210:.,9260:f,9320:.'


def saved_ram(path):
    data = path.read_bytes()
    start = 20 + struct.unpack_from('<I', data, 16)[0] * 4
    return data[start:start + 0x200000]


def word(ram, address):
    return struct.unpack_from('>h', ram, address)[0]


def pose(ram, actor):
    return tuple(word(ram, actor + offset) for offset in (4, 6, 8)) + (ram[actor + 10],)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    exe = args.exe.resolve()
    data = ROOT / 'dist/MoonstoneNative/data'
    base = [str(exe), '--os', '--mod', str(data / 'nb'),
            '--dataset', str(data), '--diskdir', str(data)]

    with tempfile.TemporaryDirectory(prefix='moonstone-practice-') as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)

        def invoke(name, extra):
            result = subprocess.run(base + ['--log', str(out / (name + '.log'))] + extra,
                                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, f'{name}: {result.stderr}\n{result.stdout}'

        fixture = out / 'practice.sav'
        invoke('boot', ['--frames', '10001', '--script', BOOT_SCRIPT,
                        '--savestate-at', '10000', str(fixture)])
        start = saved_ram(fixture)
        assert word(start, 0x3051E) == 2, 'Boot script did not select Practice'
        assert start[BLUE + 11] == 2 and start[GREEN + 11] == 1

        def run(name, script, extra=(), source=fixture, frames=250, snapshot=60):
            mid_path, end_path = out / (name + '.sav'), out / (name + '.bin')
            invoke(name, ['--loadstate', str(source), '--frames', str(frames),
                          '--script', script, '--savestate-at', str(snapshot), str(mid_path),
                          '--dumpram', str(end_path), *extra])
            return saved_ram(mid_path), end_path.read_bytes(), mid_path

        idle, idle_end, _ = run('idle', '0:.')
        old, old_end, broken_save = run('disabled-walk', '0:l,2:.', ['--noport0fix'])
        assert pose(old, GREEN) != pose(old_end, GREEN), 'Must reproduce walking after release'
        assert pose(old_end, GREEN) != pose(idle_end, GREEN)
        print('PASS: disabled fix reproduces continued wandering after release', flush=True)

        old_fire, _, _ = run('disabled-fire', '0:f', ['--noport0fix'])
        assert word(old_fire, GREEN + 0x3E) & 16, 'Must reproduce mirrored attack'
        print('PASS: disabled fix reproduces player-one fire controlling the second knight', flush=True)

        for name, script in [('left', '0:l,2:.'), ('right', '0:r,7:.'),
                             ('up', '0:u,4:.'), ('down', '0:d,3:.'), ('fire', '0:f')]:
            mid, end, _ = run(name, script)
            for state, reference in ((mid, idle), (end, idle_end)):
                assert pose(state, GREEN) == pose(reference, GREEN), f'{name}: second knight moved'
                assert word(state, GREEN + 0x3E) == 0, f'{name}: second knight received input'
                assert word(state, GREEN + 0x50) == word(reference, GREEN + 0x50)
            if name != 'fire':
                assert pose(mid, BLUE) != pose(idle, BLUE), f'{name}: player one lost movement'
            else:
                assert word(mid, BLUE + 0x3E) & 16, 'Player one lost attack'
        print('PASS: directions and attack control only player one', flush=True)

        off_parity, _, _ = run('without-parity', '0:l,2:.', ['--noretailparity'])
        assert pose(off_parity, GREEN) == pose(idle, GREEN)
        assert word(off_parity, GREEN + 0x3E) == 0
        print('PASS: input isolation also works with retail-parity patches disabled', flush=True)

        recovered, recovered_end, recovered_save = run('old-save', '0:.', source=broken_save)
        assert word(recovered, GREEN + 0x3E) == 0
        assert pose(recovered, GREEN) == pose(recovered_end, GREEN), 'Old save still walks'
        reloaded, reloaded_end, _ = run('reload', '0:.', source=recovered_save)
        assert pose(reloaded, GREEN) == pose(reloaded_end, GREEN) == pose(recovered, GREEN)
        print('PASS: old moving saves recover and save/reload retains released controls', flush=True)

        # A genuine hit must still animate/damage the waiting knight.
        _, hit_end, _ = run('hit', '0:u,34:.,45:l,115:.,140:lf,180:.,200:lf,240:.,260:lf,300:.',
                            frames=360)
        assert word(hit_end, GREEN + 0x50) < word(start, GREEN + 0x50), 'Idle knight ignores hits'
        print('PASS: waiting knight still takes combat damage', flush=True)


if __name__ == '__main__':
    main()
