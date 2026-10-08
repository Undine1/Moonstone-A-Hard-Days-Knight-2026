#!/usr/bin/env python3
"""Compare original retail scroll flow and exercise the restored flow in SDL.

All fixtures are copied, and every execution uses a scratch --log. Never deploy
the diagnostics. Native rendering/audio/wait leaves are substituted; SDL uses
the real inventory construction, transaction, input and return paths.
"""
from pathlib import Path
import argparse, json, re, shutil, struct, subprocess

ROOT = Path(__file__).resolve().parents[2]
RAM, SIZE = 104, 0x200000
get = lambda b, a, n=4: int.from_bytes(b[a:a+n], 'big')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--fixtures', type=Path, required=True,
                    help='scroll-review directory with modules, pN-inventory and old scroll saves')
    ap.add_argument('--only', choices=('native', 'fresh', 'legacy', 'boundaries', 'pending'))
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    fixtures = args.fixtures.resolve(); data = ROOT/'dist/MoonstoneNative/data'
    for name in ('inventory_flow_probe.exe', 'inventory_flow_sdl_probe.exe', 'SDL2.dll'):
        shutil.copyfile(ROOT/'recomp/build'/name, out/name)
    shutil.copyfile(ROOT/'recomp/controls.ini', out/'controls.ini')
    reports = []

    def run(name, exe, arguments):
        folder = out/name; folder.mkdir(exist_ok=True)
        result = subprocess.run([str(out/exe), *map(str, arguments), '--log', str(folder/'game.log')],
                                cwd=out, capture_output=True, text=True, timeout=45,
                                creationflags=0x08000000)
        (folder/'stdout.txt').write_text(result.stdout+result.stderr)
        assert result.returncode == 0, (name, result.stdout[-3000:], result.stderr)
        return folder, result.stdout

    if args.only in (None, 'native'):
        folder, stdout = run('native', 'inventory_flow_probe.exe', ['--dir', fixtures])
        cases = [dict(w.split('=') for w in line.split()[1:])
                 for line in stdout.splitlines() if line.startswith('ORACLE ')]
        assert len(cases) == 271
        outcomes = {}
        for row in cases:
            assert int(row['stuck']) == int(row['edition']=='3' and row['shortcut']=='1'), row
            base = 0x2e5b4 if row['edition']=='1' else 0x2e7dc
            signature = tuple(row[k] for k in ('slot', 'take', 'shortcut', 'rotate', 'owner', 'casts', 'preloot'))
            outcome = ((int(row['target'], 16)-base)//0x84, row['take_mode'])
            assert signature not in outcomes or outcomes[signature] == outcome, row
            outcomes[signature] = outcome
        (folder/'results.json').write_text(json.dumps(cases, indent=2))
        print(f'PASS {len(cases)} original/current scroll comparisons', flush=True)

    def prepared(source, folder):
        b = bytearray(source.read_bytes())
        # The copied fixture returns to this diagnostic sentinel. Give that
        # caller the same native JSR signature as actual inventory callers so
        # legacy-frame validation is exercised without bypassing its guard.
        b[RAM+0x1eeffa:RAM+0x1ef000] = bytes.fromhex('4eb90002bbec')
        path = folder/'input.sav'; path.write_bytes(b)
        return path

    def live(name, slot, owner, key, item=False, keyboard=False, source=None,
             warm=False, resume=False, checkpoint=0, finish=False, disabled=False):
        folder = out/name; folder.mkdir(exist_ok=True)
        source = source or fixtures/f'p{owner+1}-inventory.sav'
        path = prepared(source, folder)
        command = ['--os', '--sdl', '--scale', '2', '--mod', data/'nb', '--dataset', data,
                   '--diskdir', data, '--loadstate', path, '--scroll-out', folder,
                   '--slot', slot, '--owner', owner]
        if key: command += ['--shortcut']
        if item: command += ['--item']
        if not keyboard: command += ['--pad']
        if warm: command += ['--warm']
        if resume: command += ['--resume']
        if finish: command += ['--finish']
        if not resume and not finish: command += ['--rotations', '2']
        if checkpoint: command += ['--checkpoint', f'{checkpoint:x}', '--checkpoint-reload']
        if disabled: command += ['--noinventoryflow', '--noretailparity']
        folder, stdout = run(name, 'inventory_flow_sdl_probe.exe', command)
        line = next(s for s in stdout.splitlines() if s.startswith('RESULT '))
        meta = dict(w.split('=') for w in line.split()[1:])
        assert meta['returned']=='1', (name, line)
        assert meta['cursor']=='0', (name, line)
        # A restart after cursor removal may begin past the cleanup entry.
        assert int(meta['cleanups']) == (0 if finish else 1), (name, line)
        if not resume and not finish: assert meta['entries']=='1', (name, line)
        before = (path if finish else folder/'before.sav').read_bytes()[RAM:RAM+SIZE]
        panel_path = folder/'before-exit.sav'
        panel = panel_path.read_bytes()[RAM:RAM+SIZE] if panel_path.exists() else before
        after = (folder/'after-exit.sav').read_bytes()[RAM:RAM+SIZE]
        actor = get(before, 0x2fb08); target = get(panel, 0x2fb10)
        assert get(after, 0x2fb08)==actor
        for p in range(4):
            a = 0x2e7dc+p*0x84; inv = get(before, a+0x60)
            expected = bytearray(before[inv:inv+24])
            if not resume and not finish and a==actor: expected[slot] -= 1
            if slot==14 and item:
                if a==actor: expected[0] += 1
                if a==target: expected[0] -= 1
            assert after[inv:inv+24]==expected, (name, hex(a), 'inventory')
            assert before[a+0x46:a+0x54]==after[a+0x46:a+0x54], (name, 'attributes/HP/XP')
            assert before[a+0x56:a+0x60]==after[a+0x56:a+0x60], (name, 'equipment')
            if before[a+0x54:a+0x56]!=after[a+0x54:a+0x56]:
                maximum = 10+10*before[a+0x47]+20*before[inv+6]
                maximum += {0x1c:10, 0x1d:20, 0x1e:30}.get(get(before, a+0x5c), 0)
                assert get(after, a+0x54, 2)==maximum
        assert get(after, 0x2cfdc, 2)==0
        assert get(after, 0x2e9ec+0x64)==(target if slot==16 else 0), (name, 'dragon target')
        owners = set(map(int, re.findall(r'MP-CAMPAIGN context=\d+ owner=P(\d+)',
                                        (folder/'game.log').read_text())))
        assert owners=={owner+1}, (name, owners)
        reports.append(dict(case=name, result=line, gameplay_and_owner_verified=True))
        print(f'PASS {name}: one inventory return; items/stats/target/ownership correct', flush=True)
        return folder

    if args.only in (None, 'fresh'):
        for slot in (14, 16):
            for owner in range(4):
                for key in (False, True):
                    for item in (False, True):
                        live(f'fresh-s{slot}-p{owner+1}-key{int(key)}-item{int(item)}',
                             slot, owner, key, item, keyboard=owner%2==0, warm=owner>=2)

    if args.only in (None, 'legacy'):
        for slot in (14, 16):
            for key in (False, True):
                for taken in (False, True):
                    source = fixtures/f's{slot}-x0-item{int(taken)}-warm1-p3-pad1-rot2/before-exit.sav'
                    live(f'legacy-s{slot}-key{int(key)}-taken{int(taken)}', slot, 2, key,
                         keyboard=key, source=source, resume=True)

    if args.only in (None, 'boundaries'):
        for slot in (14, 16):
            for pc in (0x2cf18 if slot==14 else 0x2cf96, 0x2bc5c, 0x2bc6a, 0x2bc1e, 0x2bc72, 0x2bca2):
                folder = live(f'warm-s{slot}-{pc:x}', slot, 2, True,
                              checkpoint=pc)
                # Boundary before dispatch still owns a consumed scroll but
                # has not changed the screen mode. Resume completes it natively.
                live(f'cold-s{slot}-{pc:x}', slot, 2, True, keyboard=True,
                     source=folder/'boundary.sav', resume=pc not in (0x2bc72, 0x2bca2),
                     finish=pc in (0x2bc72, 0x2bca2))
    if args.only in (None, 'pending'):
        for slot in (14, 16):
            live(f'pending-options-s{slot}', slot, 2, True, keyboard=True, resume=True,
                 source=out/f'warm-s{slot}-2bc5c/boundary.sav', disabled=True)
    (out/f'results-{args.only or "all"}.json').write_text(json.dumps(reports, indent=2))
    print(f'PASS {len(reports)} SDL scroll scenarios', flush=True)


if __name__ == '__main__':
    main()
