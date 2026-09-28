#!/usr/bin/env python3
"""Native retail effect, interrupt safety, rendering and save/load checks.

Build task_effect_probe.c separately; never deploy the diagnostic executable.
Reference files are read-only. Every game invocation has a scratch --log.
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
    ap.add_argument('--probe', type=Path, default=ROOT / 'recomp/build/task_effect_probe.exe')
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--reference', type=Path, default=ROOT / 'recomp/build/task-timing-20260923')
    ap.add_argument('--purpose', type=Path, default=ROOT / 'recomp/build/effect-purpose-20260923')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--native-only', action='store_true')
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    reports = []

    def run(name, fixture, frames, extra=(), exe=None):
        folder = out / name
        folder.mkdir(exist_ok=True)
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(fixture),
                   '--frames', str(frames), '--log', str(folder / 'game.log'),
                   '--dumpram', str(folder / 'final.ram'), *map(str, extra)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120,
                                creationflags=0x08000000)
        (folder / 'stdout.txt').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        log = (folder / 'game.log').read_text(errors='replace')
        assert not any(word in log for word in ('TASK-TRACE HOLE', 'TASK-TRACE DUPLICATE', 'TASKFIX-FULL')), name
        assert 'unmapped=0' in result.stdout, (name, result.stdout[-1500:])
        return folder, result.stdout

    native, stdout = run('native', args.reference / 'current.sav', 0,
                        ['--task-tests', args.reference, '--task-output', out], args.probe)
    print(stdout[stdout.index('PASS:'):], flush=True)
    if args.native_only:
        return

    # Real attack AI and scripts, including repeats and effects crossing captures.
    cases = [c for c in json.loads((args.purpose / 'scenario-results.json').read_text())
             if c['case'] == 'pre-balok']
    cases += json.loads((args.purpose / 'kind40-results.json').read_text())
    for case in cases:
        extra = ['--poke', f"0x12cb32:{case['options'][1]}:w@0x25a10",
                 '--script', case['options'][3]]
        fixture = ROOT / 'dist/MoonstoneNative' / case['fixture']
        with ThreadPoolExecutor(max_workers=2) as pool:
            before = pool.submit(run, case['case'] + '-before', fixture, case['frames'], extra, args.before_exe)
            after = pool.submit(run, case['case'] + '-after', fixture, case['frames'], [*extra, '--task-trace'], args.probe)
            old, _ = before.result()
            new, traced = after.result()
        # Ordinary encounters in this fixture have no overlapping starts; every
        # bit of gameplay RAM must agree, not just actor HP/positions.
        assert (old / 'final.ram').read_bytes() == (new / 'final.ram').read_bytes(), case['case']
        stats = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', traced.split('TASK-SUMMARY')[-1])}
        assert stats['starts'] > 0 and stats['overlaps'] == stats['holes'] == stats['duplicates'] == 0, stats
        reports.append({'case': case['case'], 'frames': case['frames'], 'full_RAM_equal': True, **stats})
        print('PASS encounter full RAM:', case['case'], stats, flush=True)

    def read_ppm(path):
        header, geometry, depth, pixels = path.read_bytes().split(b'\n', 3)
        assert (header, geometry, depth) == (b'P6', b'320 200', b'255'), path
        return pixels

    # Pixel-exact image comparison on real attacks: only the original vertical
    # offsets are permitted. No resizing, horizontal movement or new timing.
    offsets = (0, 8, -8, 2, -2, 1, -1)
    for name, relative in [('balok', 'pre-balok/effect-1.sav'), ('troll', 'kind40-approach/effect-3.sav')]:
        fixture = args.purpose / relative
        folders = []
        for suffix, exe in [('before', args.before_exe), ('after', args.exe)]:
            dest = out / (name + '-' + suffix)
            folder, _ = run(name + '-' + suffix, fixture, 45,
                            ['--script', '0:.', '--dump', dest / 'frame', '--dumpevery', 1], exe)
            folders.append(folder)
        assert (folders[0] / 'final.ram').read_bytes() == (folders[1] / 'final.ram').read_bytes()
        observed = []
        for frame in range(45):
            before, after = [read_ppm(folder / f'frame_{frame:04}.ppm') for folder in folders]
            matches = []
            for dy in offsets:
                visible_old = before[max(0, -dy) * 960:min(200, 200 - dy) * 960]
                visible_new = after[max(0, dy) * 960:min(200, 200 + dy) * 960]
                if visible_old == visible_new:
                    matches.append(dy)
            assert len(matches) == 1, (name, frame, matches)
            observed.append(matches[0])
        assert set(observed) == set(offsets) and observed[-1] == 0, (name, observed)
        for dy in offsets[1:]:
            assert observed.count(dy) == 3, (name, dy, observed)
        reports.append({'case': name + '-pixels', 'offsets_per_frame': observed})
        print('PASS exact original displacement/duration:', name, observed, flush=True)

    # Cold resumes at each frame of a live effect. Compare against a continuous
    # run using the same normal production binary, plus the old-save start above.
    fixture = args.purpose / 'kind40-approach/effect-3.sav'
    continuous, _ = run('continuous', fixture, 100, ['--script', '0:.', '--dump', out / 'continuous.ppm'])
    expected = (continuous / 'final.ram').read_bytes()
    for frame in range(36):
        save = out / f'cold-{frame}.sav'
        run(f'checkpoint-{frame}', fixture, frame + 1,
            ['--script', '0:.', '--savestate-at', frame, save])
        resumed, _ = run(f'resume-{frame}', save, 100 - frame,
                         ['--script', '0:.', '--dump', out / f'resume-{frame}.ppm'])
        assert (resumed / 'final.ram').read_bytes() == expected, ('cold resume', frame)
        assert (out / f'resume-{frame}.ppm').read_bytes() == (out / 'continuous.ppm').read_bytes(), frame
    reports.append({'case': 'cold-resume', 'boundaries': 36, 'full_RAM_and_final_image_equal': True})
    print('PASS: 36 live mid-effect cold resumes match continuous RAM and restored image', flush=True)
    (out / 'results.json').write_text(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
