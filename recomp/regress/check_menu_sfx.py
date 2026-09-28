#!/usr/bin/env python3
"""Replay retail UI clicks, rejected actions and saves interrupted during sound.

Uses the read-only fixtures documented in moonstone-menu-sounds.md. All output,
including every game log, goes to a separate scratch directory. Build the
test-only menu_sfx_probe.c as well as the production executable first.
"""
import argparse
import array
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[2]
RAM_OFFSET, RAM_SIZE = 104, 0x200000


def get(ram, address, size=4):
    return int.from_bytes(ram[address:address + size], 'big')


def gameplay(ram):
    """Persistent actors/items and name field, excluding audio/scratch stacks."""
    actors = [0x2e7dc + n * 0x84 for n in range(4)]
    return {'actors': [ram[a:a + 0x84].hex() for a in actors],
            'items': [ram[get(ram, a + 0x60):get(ram, a + 0x60) + 0x18].hex() for a in actors],
            'temple': ram[0x2fb20:0x2fb38].hex(),
            'mode': get(ram, 0x2fb1c), 'name_open': get(ram, 0x2e05c, 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT / 'recomp/build/menu_sfx_probe.exe')
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--fixtures', type=Path, default=ROOT / 'recomp/build/menu-sounds-20260923')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out, fixtures = args.output.resolve(), args.fixtures.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    reports = []

    def run(name, fixture, frames, script='0:.', typed=None, *, exe=None, extra=(), pcm=True):
        command = [str((exe or args.probe).resolve()), '--os', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(fixture),
                   '--frames', str(frames), '--script', script, '--log', str(out / (name + '.log')),
                   '--dumpram', str(out / (name + '.ram')), *extra]
        if pcm:
            command += ['--wav', str(out / (name + '.wav'))]
        if typed:
            command += ['--type', typed]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                                creationflags=0x08000000)
        (out / (name + '.stdout')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        log = (out / (name + '.log')).read_text(errors='replace')
        ram = (out / (name + '.ram')).read_bytes()
        assert len(ram) == RAM_SIZE and 'unmapped=0' in result.stdout, name
        row = {'case': name, 'command': command, 'gameplay': gameplay(ram),
               'requests': re.findall(r'^SFX-REQUEST .*', log, re.M),
               'injections': re.findall(r'^SFX-CLICK .*', log, re.M),
               'ram_sha256': hashlib.sha256(ram).hexdigest()}
        if pcm:
            wav_path = out / (name + '.wav')
            with wave.open(str(wav_path), 'rb') as wav:
                samples = array.array('h', wav.readframes(wav.getnframes()))
                row['pcm_nonzero'] = sum(v != 0 for v in samples)
            row['wav_sha256'] = hashlib.sha256(wav_path.read_bytes()).hexdigest()
        reports.append(row)
        return row, ram, log

    cases = [(name, fixtures / (name + '.sav'), 260, '190:f,198:.', None)
             for name in ('armour-1', 'armour-2', 'armour-3', 'weapon-1', 'weapon-2',
                          'daggers', 'temple-buy')]
    cases.append(('temple-sell', fixtures / 'temple-sell.sav', 130, '10:f,18:.', None))
    for town, labels in [('waterdeep', ('merchant', 'tavern', 'healer', 'mystic', 'exit')),
                         ('highwood', ('merchant', 'tavern', 'healer', 'temple', 'exit'))]:
        cases += [(town + '-' + label, fixtures / (town + '-' + label + '.sav'),
                   100, '10:f,18:.', None) for label in labels]
    for name, relative in [
        ('ordinary-sword-loot', 'sword-fix-20260921/focused/ui-4-16-17.sav'),
        ('special-sword-loot', 'sword-fix-20260921/focused/ui-4-16-19.sav'),
        ('armour-loot', 'manual-armour-20260921/final-check/ui-l4-p0-1b-1e.sav'),
    ]:
        cases.append((name, ROOT / 'recomp/build' / relative, 260, '190:f,198:.', None))
    cases += [('name-fire', fixtures / 'name-wait.sav', 150, '50:f,58:.', None),
              ('name-enter', fixtures / 'name-wait.sav', 150, '0:.', '50:!'),
              ('main-menu', ROOT / 'recomp/build/mixed-host-final/menu.sav', 150,
               '10:r,18:.,40:d,48:.,70:f,78:.', None)]
    actual = {}
    for case in cases:
        name = case[0]
        row, ram, _ = run(*case)
        before, _, _ = run('before-' + name, *case[1:], exe=args.before_exe)
        assert row['gameplay'] == before['gameplay'], name + ': gameplay changed'
        assert len(row['requests']) == (name != 'main-menu'), (name, row['requests'])
        if name != 'main-menu':
            assert row['pcm_nonzero'], name + ': silent PCM'
            # Check the acceptance itself, not music from the next scene or
            # the tail of the preceding knight-choice sound in the name save.
            start, length = (45, 45) if name.startswith('name-') else (10, 10)
            if name.startswith(('name-', 'waterdeep-', 'highwood-')):
                with wave.open(str(out / (name + '.wav')), 'rb') as wav:
                    wav.setpos(start * 882)
                    assert any(wav.readframes(length * 882)), name + ': acceptance window silent'
        else:
            assert not row['pcm_nonzero'] and row['wav_sha256'] == before['wav_sha256']
        if name.startswith('name-'):
            assert row['gameplay']['name_open'] == 0
        actual[name] = row
    print('PASS: 24 real UI replays; successful actions click once, transactions/actors unchanged, main menu silent', flush=True)

    # Real production binary must produce exactly the traced RAM and PCM.
    for case in cases:
        if case[0] in ('daggers', 'waterdeep-merchant', 'ordinary-sword-loot', 'temple-buy', 'name-enter'):
            row, _, _ = run('production-' + case[0], *case[1:], exe=args.exe)
            assert all(row[k] == actual[case[0]][k] for k in ('ram_sha256', 'wav_sha256'))
    print('PASS: five production-binary full RAM/PCM comparisons', flush=True)

    # Disabled layers retain the previous executable's complete behavior.
    for flag in ('--noretailsfx', '--noretailparity'):
        for case in cases:
            if case[0] not in ('daggers', 'waterdeep-merchant', 'ordinary-sword-loot', 'temple-buy', 'name-enter'):
                continue
            name = flag[2:] + '-' + case[0]
            row, _, _ = run(name, *case[1:], exe=args.exe, extra=[flag])
            old, _, _ = run('before-' + name, *case[1:], exe=args.before_exe, extra=[flag])
            assert all(row[k] == old[k] for k in ('ram_sha256', 'wav_sha256')), name
    print('PASS: ten disabled-layer full RAM/PCM comparisons', flush=True)

    # Seed only prerequisites, then let actual hotspot validation reject input.
    rejected = []
    for case in cases[:7]:
        rejected.append(('no-money-' + case[0], case, [(0x2e7dc + 0x4a, 0, 2)]))
    rejected += [
        ('daggers-capped', cases[5], [(0x2e7dc + 0x4c, 10, 1)]),
        ('temple-empty', cases[6], [(0x2fb24, 0, 1)]),
        ('base-weapon', next(c for c in cases if c[0] == 'ordinary-sword-loot'), [(0x2e8e4 + 0x58, 0x16, 4)]),
    ]
    for name, case, patches in rejected:
        save = bytearray(case[1].read_bytes())
        for address, value, size in patches:
            save[RAM_OFFSET + address:RAM_OFFSET + address + size] = value.to_bytes(size, 'big')
        fixture = out / (name + '.sav')
        fixture.write_bytes(save)
        row, _, _ = run(name, fixture, *case[2:])
        before, _, _ = run('before-' + name, fixture, *case[2:], exe=args.before_exe)
        assert not row['requests'] and not row['injections'], name
        assert row['gameplay'] == before['gameplay'], name
    print('PASS: ten rejected/empty/capped UI actions stay silent and preserve gameplay', flush=True)

    for case in cases:
        if case[0] not in ('ordinary-sword-loot', 'special-sword-loot', 'armour-loot', 'temple-buy', 'temple-sell'):
            continue
        name, fixture = case[:2]
        script = '10:f,18:.,70:f,78:.' if name == 'temple-sell' else '190:f,198:.,260:f,268:.'
        row, _, _ = run('repeat-' + name, fixture, 310, script)
        assert len(row['requests']) == 1 and row['gameplay'] == actual[name]['gameplay'], name
    print('PASS: repeat clicks cannot duplicate ordinary/special weapon, armour or temple transfers/sounds', flush=True)

    # The probe tests all registers and every CCR for all25 injected sites. It
    # also warm-loads a save at each native instruction, with both switches off.
    boundary_dir = out / 'boundaries'
    boundary_dir.mkdir(exist_ok=True)
    row, _, _ = run('native-matrix', fixtures / 'temple-buy.sav', 260, '190:f,198:.',
                    extra=['--sfx-tests', str(boundary_dir), '--sfx-capture', str(boundary_dir)])
    assert 'PASS: all25' in (out / 'native-matrix.stdout').read_text()
    boundaries = sorted(boundary_dir.glob('mid-*.sav'))
    assert len(boundaries) > 5
    for fixture in boundaries:
        assert struct.unpack_from('<I', fixture.read_bytes(), 8)[0] == 5
        row, _, _ = run('cold-' + fixture.stem, fixture, 70, pcm=False, exe=args.exe)
        assert row['gameplay'] == actual['temple-buy']['gameplay'], fixture.name
        assert not row['injections'], fixture.name + ': reinjected sound'
    for fixture in (boundaries[0], boundaries[len(boundaries) // 2], boundaries[-1]):
        for flag in ('--noretailsfx', '--noretailparity'):
            row, _, _ = run('cold-' + fixture.stem + flag[2:], fixture, 70, pcm=False,
                            exe=args.exe, extra=[flag])
            assert row['gameplay'] == actual['temple-buy']['gameplay'] and not row['injections']
    print(f'PASS: {len(boundaries)} cold instruction-boundary loads finish the real temple purchase; v5 and disabled modes work', flush=True)
    (out / 'results.json').write_text(json.dumps({
        'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        'before_sha256': hashlib.sha256(args.before_exe.read_bytes()).hexdigest(),
        'cold_boundaries': len(boundaries), 'cases': reports}, indent=2))


if __name__ == '__main__':
    main()
