#!/usr/bin/env python3
"""Check native retail cursor RNG, interrupted saves and real menu replays.

Build cursor_rng_probe.c separately. Never deploy diagnostic executables.
Every process logs into scratch output; all source fixtures are read-only.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT / 'recomp/build/cursor_rng_probe.exe')
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    reference = ROOT / 'recomp/build/task-timing-20260923'
    data = ROOT / 'dist/MoonstoneNative/data'
    reports = []

    def run(name, fixture, frames, script=None, extra=(), exe=None, images=False):
        folder = out / name
        folder.mkdir(exist_ok=True)
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(fixture),
                   '--frames', str(frames), '--log', str(folder / 'game.log'),
                   '--dumpram', str(folder / 'final.ram'), '--memlog', *map(str, extra)]
        if script is not None:
            command += ['--script', script]
        if images:
            command += ['--dump', str(folder / 'frame'), '--dumpevery', '10',
                        '--wav', str(folder / 'audio.wav')]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120,
                                creationflags=0x08000000)
        (folder / 'stdout.txt').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        return folder

    native = out / 'native'
    native.mkdir(exist_ok=True)
    run('native', reference / 'current.sav', 0,
        extra=['--rng-tests', reference, '--rng-output', native], exe=args.probe)
    stdout = (native / 'stdout.txt').read_text()
    print(stdout[stdout.index('PASS:'):], flush=True)
    expected = (native / 'expected.ram').read_bytes()
    boundaries = sorted(native.glob('boundary-*.sav'))

    def cold(fixture):
        dest = out / ('cold-' + fixture.stem)
        folder = run(dest.name, fixture, 0, extra=['--rng-resume', dest / 'resumed.ram'], exe=args.probe)
        assert (folder / 'resumed.ram').read_bytes() == expected, fixture.name

    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(cold, boundaries))
    print('PASS:', len(boundaries), 'cold native resumes match uninterrupted full RAM and caller registers', flush=True)
    reports.append({'case': 'native-cold-resume', 'boundaries': len(boundaries), 'full_RAM_equal': True})

    # Reproduce each changed golden and inspect visual/transaction consequences.
    cases = [
        ('combatrun', ROOT / 'dist/MoonstoneNative/moonstone_combatrun.sav', 600, None, []),
        ('townday', ROOT / 'dist/MoonstoneNative/moonstone_townmap.sav', 800,
         '10:f,16:.,450:d,494:.,504:f,510:.', []),
        ('townarmor', ROOT / 'dist/MoonstoneNative/moonstone_townarmor.sav', 2600,
         '10:u,36:.,40:r,46:.,60:f,66:.,800:d,844:.,850:l,853:.,860:f,866:.',
         ['--poke8', '700:2e83b:1e']),
        ('cursor-idle', reference / 'current.sav', 200, '0:.', []),
        ('frontend', ROOT / 'recomp/build/mixed-host-final/menu.sav', 150,
         '10:r,18:.,40:d,48:.,70:f,78:.', []),
    ]
    for name, fixture, frames, script, extra in cases:
        folders = []
        for suffix, exe, flags in [('before', args.before_exe, []),
                                   ('control', args.exe, ['--nocursorrng']),
                                   ('after', args.probe, ['--rng-trace'])]:
            folders.append(run(name + '-' + suffix, fixture, frames, script,
                               [*extra, *flags], exe=exe, images=True))
        before, control, after = folders
        for path in before.glob('*'):
            if path.suffix in ('.ram', '.ppm', '.wav'):
                assert path.read_bytes() == (control / path.name).read_bytes(), (name, path.name, 'A/B drift')
        old, new = [(folder / 'final.ram').read_bytes() for folder in (before, after)]
        delta = [i for i in range(len(old)) if old[i] != new[i]]
        ranges = []
        for address in delta:
            if not ranges or ranges[-1][1] + 1 != address:
                ranges.append([address, address])
            else:
                ranges[-1][1] = address
        changed_images = [path.name for path in before.glob('*.ppm')
                          if path.read_bytes() != (after / path.name).read_bytes()]
        trace = (after / 'stdout.txt').read_text().split('RNG-SUMMARY')[-1]
        stats = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', trace)}
        assert stats['cursor_ticks'] == stats['added_calls'], (name, stats)
        draws = re.findall(r'^RNG-DRAW .*', (after / 'game.log').read_text(), re.M)
        assert sum('caller=3b91c ' in line for line in draws) == stats['added_calls']
        if name == 'frontend':
            assert stats['added_calls'] == 0 and not delta and not changed_images
        else:
            assert stats['added_calls'] > 0
        row = {'case': name, 'frames': frames, 'disabled_matches_before_RAM_images_PCM': True,
               'RAM_changed_bytes': len(delta),
               'RAM_changed_ranges': [[hex(a), hex(b)] for a, b in ranges],
               'changed_images': changed_images,
               'PCM_equal': (before / 'audio.wav').read_bytes() == (after / 'audio.wav').read_bytes(),
               'before_seed': old[0x391a8:0x391ac].hex(), 'after_seed': new[0x391a8:0x391ac].hex(),
               'after_RAM_sha256': hashlib.sha256(new).hexdigest(), **stats}
        bus_counts = [int(re.search(r'unmapped=(\d+)', (f / 'stdout.txt').read_text())[1]) for f in folders]
        assert bus_counts[0] == bus_counts[1], (name, bus_counts)
        row['unmapped_accesses_before_control_after'] = bus_counts
        if name in ('townday', 'townarmor'):
            # Some old AI paths read an absent inventory while choosing whom
            # to chase. Those unused reads also exist in retail. Reproduce the
            # entire new path in the frozen executable by supplying only the
            # post-menu seed at its first foreground RNG call.
            first = next(line for line in draws if 'caller=3b91c ' not in line)
            caller = int(re.search(r'caller=(\w+)', first)[1], 16)
            seed = int(re.search(r'before=(\w+)', first)[1], 16)
            seeded = run(name + '-seed-control', fixture, frames, script,
                         [*extra, '--poke', f'0x391a8:0x{seed:x}:l@0x{caller-6:x}'],
                         exe=args.before_exe, images=True)
            assert int(re.search(r'unmapped=(\d+)', (seeded / 'stdout.txt').read_text())[1]) == bus_counts[2]
            assert (seeded / 'audio.wav').read_bytes() == (after / 'audio.wav').read_bytes()
            assert all(p.read_bytes() == (after / p.name).read_bytes() for p in seeded.glob('*.ppm'))
            seeded_ram = (seeded / 'final.ram').read_bytes()
            remaining = [i for i in range(len(new)) if seeded_ram[i] != new[i]]
            # Cached JOY0 decode and previous mouse counter can differ with
            # polling timing; the complete remaining RAM must match.
            assert set(remaining) <= {0x2ebc4, 0x2ebc5, 0x3bf80, 0x3bf81}, (name, remaining)
            row['old_executable_seed_control'] = {'seed': hex(seed), 'site': hex(caller-6),
                'same_bus_reads_images_PCM': True, 'remaining_RAM_bytes': [hex(i) for i in remaining]}
        reports.append(row)
        print('PASS replay:', name, stats, 'changed bytes', len(delta),
              'changed images', len(changed_images), 'PCM equal', row['PCM_equal'], flush=True)
        (out / 'results.json').write_text(json.dumps(reports, indent=2))

    # The RAM goldens run without audio rendering; measure those exact modes
    # separately, because native audio IRQs change guest execution when enabled.
    for name, fixture, frames, script, extra in cases[:3]:
        folders = [run(name + '-golden-' + suffix, fixture, frames, script,
                       [*extra, *flags], exe=exe)
                   for suffix, exe, flags in [('before', args.before_exe, []),
                                              ('control', args.exe, ['--nocursorrng']),
                                              ('after', args.probe, ['--rng-trace'])]]
        old, control, new = [(f / 'final.ram').read_bytes() for f in folders]
        assert old == control, name
        delta = [i for i in range(len(old)) if old[i] != new[i]]
        row = {'case': name + '-golden', 'frames': frames, 'A/B_full_RAM_equal': True,
               'changed_bytes': {hex(i): [old[i], new[i]] for i in delta},
               'hashes': [re.search(r'ram_fnv=(\w+)', (f / 'stdout.txt').read_text())[1] for f in folders]}
        reports.append(row)
        print('PASS golden A/B:', name, row['hashes'], 'changed bytes', len(delta), flush=True)
    (out / 'results.json').write_text(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
