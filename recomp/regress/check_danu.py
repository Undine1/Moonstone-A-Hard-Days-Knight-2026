#!/usr/bin/env python3
"""Check Stonehenge retail parity, legitimate offers, XP and interrupted saves.

Uses local original-module and UI fixtures from the documented September23
investigation. All saves, logs and PCM go to --output, never an installation.
Build danu_probe.c with the normal loader/Musashi/SDL sources; never deploy it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RAM_OFFSET, RAM_SIZE = 104, 0x200000
ALLOWED = {0, 2, 6, 8, 10, 12, 14, 16, 18}
RETAIL_ALLOWED = ALLOWED - {6, 8}


def get(ram, address, size=4):
    return int.from_bytes(ram[address:address + size], 'big')


def put(ram, address, value, size=4):
    ram[address:address + size] = value.to_bytes(size, 'big')


def saved_ram(save):
    return bytearray(save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE])


def persistent(ram):
    actors = [0x2e7dc + n * 0x84 for n in range(4)]
    return {'actors': [ram[a + 0x46:a + 0x84].hex() for a in actors],
            'items': [ram[get(ram, a + 0x60):get(ram, a + 0x60) + 0x18].hex() for a in actors],
            'scene': get(ram, 0x2fb1c), 'selected': get(ram, 0x2cfda, 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT / 'recomp/build/danu_probe.exe')
    ap.add_argument('--fixtures', type=Path, default=ROOT / 'recomp/build/item-action-20260923')
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out, fixtures = args.output.resolve(), args.fixtures.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    reports = []

    def execute(command, name, timeout=60):
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout,
                                creationflags=0x08000000)
        (out / (name + '.stdout')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        return result

    def run(name, save, frames, script='0:.', extra=(), exe=None):
        path = out / (name + '.sav')
        path.write_bytes(save)
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                   '--frames', str(frames), '--script', script,
                   '--log', str(out / (name + '.log')),
                   '--dumpram', str(out / (name + '.ram')), *extra]
        result = execute(command, name)
        assert 'unmapped=0' in result.stdout, name
        ram = (out / (name + '.ram')).read_bytes()
        assert len(ram) == RAM_SIZE, name
        reports.append({'case': name, 'command': command,
                        'ram_sha256': hashlib.sha256(ram).hexdigest(), 'state': persistent(ram)})
        return ram

    command = [str(args.probe.resolve()), '--danu-oracle', str(fixtures),
               '--log', str(out / 'oracle.jsonl')]
    execute(command, 'oracle')
    rows = [json.loads(line) for line in (out / 'oracle.jsonl').read_text().splitlines()
            if line.startswith('{')]
    assert len(rows) == 2432, len(rows)
    current, references = [], {}
    for row in rows:
        edition, arg = row['edition'], row['arg']
        if edition == 0:
            # The archived original is a positive control for the inherited bug.
            assert row['selected'] != 65535 or arg in (20, 22), row
            continue
        allowed = ALLOWED if edition == 2 else RETAIL_ALLOWED
        accepted = arg in allowed
        upgrade = arg in (0x46, 0x47, 0x48) and row['xp'] >= row['price']
        assert row['selected'] == (arg if accepted else 65535), row
        assert row['exit'] == int(accepted), row
        assert row['clicks'] == int(accepted or upgrade), row
        assert row['writes'] == ([[arg, 1, 0]] if accepted else []), row
        assert row['counter'] == 0, row
        assert row['lives'] == min(row['lives_before'] + int(accepted), 5), row
        assert row['curse'] == (0 if accepted else 1), row
        assert row['weapon'] == (0x19 if row['sword'] else 0x16), row
        assert row['xp_after'] == row['xp'] - (row['price'] if upgrade else 0), row
        expected_stats = [2, 2, 2]
        if upgrade:
            expected_stats[arg - 0x46] += 1
        assert row['stats'] == expected_stats, row
        if accepted:
            assert row['hp'] == row['max_hp'] == (30 if arg == 6 else 50), row
        # Retail reordered the Constitution/Endurance display actions. Compare
        # their actual actor fields, not those presentation action numbers.
        key = tuple(row[k] for k in ('player', 'sword', 'xp', 'lives_before', 'arg'))
        if edition == 1:
            references[key] = row
        else:
            current.append((key, row))
    fields = ('selected', 'exit', 'clicks', 'stats', 'xp_after', 'lives', 'hp',
              'max_hp', 'curse', 'weapon', 'counter', 'writes')
    compared = 0
    for key, row in current:
        reference = references[key]
        if row['arg'] not in (6, 8):
            assert all(row[field] == reference[field] for field in fields), (row, reference)
            compared += 1
    print(f'PASS: {len(rows)} original/current native cases; {compared} exact retail results; labelled ring/talisman exception', flush=True)

    def entry(name, sword=False, player=0, xp=0, lives=3, copies=1):
        save = bytearray((fixtures / 'checked-entry.sav').read_bytes())
        ram = saved_ram(save)
        actor = 0x2e7dc + player * 0x84
        items = get(ram, actor + 0x60)
        put(ram, 0x2e0bc, actor)
        for offset in ALLOWED:
            put(ram, items + offset, copies, 1)
        put(ram, items + 4, int(sword), 1)
        put(ram, actor + 0x58, 0x19 if sword else 0x16)
        put(ram, actor + 0x4e, xp, 2)
        put(ram, actor + 0x49, lives, 1)
        put(ram, actor + 0x50, 13, 2)
        put(ram, actor + 0x82, 1, 1)
        for offset in (0x46, 0x47, 0x48):
            put(ram, actor + offset, 2, 1)
        save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
        run(name + '-entry', save, 400, '60:f,68:.',
            ['--savestate-at', '399', str(out / (name + '-wait.sav'))])
        return (out / (name + '-wait.sav')).read_bytes()

    cases = [('armour', 110, 116, None), ('gold', 185, 44, None),
             ('strength', 127, 37, None), ('constitution', 127, 44, None),
             ('endurance', 127, 51, None), ('dagger', 113, 83, None),
             ('life', 120, 65, None), ('ordinary-sword', 134, 99, None),
             ('potion', 109, 168, 0), ('ring', 109, 153, 6),
             ('talisman', 149, 145, 8), ('gem', 166, 170, 2),
             ('haste', 108, 188, 10), ('hawk', 132, 188, 12),
             ('acquisition', 157, 188, 14), ('wyrm', 182, 188, 16),
             ('protection', 207, 188, 18), ('key', 155, 116, None)]

    def click(name, save, x, y, offset, *, empty=False, extra=(), exe=None):
        seeded = bytearray(save)
        struct.pack_into('>HH', seeded, RAM_OFFSET + 0x392d4, x, y)
        before = saved_ram(seeded)
        actor, items = get(before, 0x2fb08), get(before, 0x2fb0c)
        if empty:
            assert offset in ALLOWED
            put(before, items + offset, 0, 1)
            seeded[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = before
        accepted = offset is not None and not empty
        ram = run(name, seeded, 1250 if accepted else 80, '10:f,18:.', extra, exe)
        assert get(ram, 0x2cfda, 2) == (offset if accepted else 65535), name
        assert get(ram, 0x2fb1c) == (9 if accepted else 3), name
        all_items = get(before, 0x2e7dc + 0x60)
        delta = [[i, before[all_items + i], ram[all_items + i]] for i in range(0x60)
                 if before[all_items + i] != ram[all_items + i]]
        expected = [[items - all_items + offset, before[items + offset], before[items + offset] - 1]] if accepted else []
        assert delta == expected, (name, delta, expected)
        assert ram[actor + 0x46:actor + 0x49] == before[actor + 0x46:actor + 0x49], name
        assert ram[actor + 0x4a:actor + 0x50] == before[actor + 0x4a:actor + 0x50], name
        assert ram[actor + 0x58:actor + 0x64] == before[actor + 0x58:actor + 0x64], name
        assert get(ram, actor + 0x49, 1) == min(get(before, actor + 0x49, 1) + int(accepted), 5), name
        assert get(ram, actor + 0x82, 1) == (0 if accepted else get(before, actor + 0x82, 1)), name
        if accepted:
            assert get(ram, actor + 0x50, 2) == get(ram, actor + 0x54, 2), name
        for other in range(4):
            other_actor = 0x2e7dc + other * 0x84
            if other_actor != actor:
                assert ram[other_actor:other_actor + 0x84] == before[other_actor:other_actor + 0x84], (name, other)
        return seeded, ram

    fresh = entry('fresh')
    cached = (fixtures / 'checked-wait.sav').read_bytes()
    for title, save in [('fresh', fresh), ('cached', cached)]:
        for label, x, y, offset in cases:
            click(title + '-' + label, save, x, y, offset)
        for label, x, y, offset in cases:
            if offset in ALLOWED:
                click(title + '-empty-' + label, save, x, y, offset, empty=True)
    fresh_sword = entry('sword', sword=True)
    for title, save in [('fresh', fresh_sword), ('cached', (fixtures / 'checked-sword-wait.sav').read_bytes())]:
        click(title + '-special-sword', save, 134, 99, None)
    for player in (1, 2, 3):
        save = entry('player-' + str(player + 1), player=player, copies=2, lives=5)
        click('player-' + str(player + 1) + '-ring', save, 109, 153, 6)
    print('PASS: new/old offering panels, all nine legitimate items, empty cached icons, invalid items, sword, four knights and five-life cap', flush=True)

    # Stat upgrades remain separate from offerings. Exit afterwards must NOT
    # run the ritual or change lives, items, curse or equipment.
    stats = entry('xp', xp=3)
    for label, y, stat in [('strength', 37, 0x46), ('constitution', 44, 0x47), ('endurance', 51, 0x48)]:
        seed = bytearray(stats)
        struct.pack_into('>HH', seed, RAM_OFFSET + 0x392d4, 127, y)
        before = saved_ram(seed)
        actor = get(before, 0x2fb08)
        after = out / ('xp-' + label + '-after.sav')
        ram = run('xp-' + label, seed, 80, '10:f,18:.', ['--savestate-at', '79', str(after)])
        assert get(ram, actor + stat, 1) == get(before, actor + stat, 1) + 1, label
        assert get(ram, actor + 0x4e, 2) == 3 - get(before, 0x30528, 2), label
        assert get(ram, 0x2cfda, 2) == 65535 and get(ram, 0x2fb1c) == 3, label
        exit_save = bytearray(after.read_bytes())
        struct.pack_into('>HH', exit_save, RAM_OFFSET + 0x392d4, 83, 96)
        ended = run('xp-' + label + '-exit', exit_save, 1250, '10:f,18:.')
        assert get(ended, 0x2fb1c) == 9 and get(ended, actor + 0x49, 1) == 3, label
        assert get(ended, actor + 0x82, 1) == 1, label
        assert persistent(ended)['items'] == persistent(before)['items'], label
    print('PASS: real XP upgrades and Exit do not become life rewards or offerings', flush=True)

    # Both A/B gates reproduce the frozen previous executable byte-for-byte.
    for name in ('armour', 'potion', 'special-sword'):
        seed = (fixtures / ('checked-' + name + '.sav')).read_bytes()
        old = run('before-' + name, seed, 1250, '10:f,18:.', exe=args.before_exe)
        for flag in ('--nodanufix', '--noretailparity'):
            # Other parity fixes also switch off with the master flag, so compare
            # that mode on both builds, not to an enabled baseline.
            baseline = old if flag == '--nodanufix' else run('before-parity-off-' + name, seed, 1250, '10:f,18:.', [flag], args.before_exe)
            result = run(flag[2:] + '-' + name, seed, 1250, '10:f,18:.', [flag])
            assert result == baseline, name + flag
    print('PASS: isolated and master parity-off controls match the frozen build exactly', flush=True)

    # Save at actual instruction boundaries in an unmodified running offering.
    boundaries = 0
    for name, seed in [('potion', fresh), ('ring', fresh), ('sword', fresh_sword)]:
        if name == 'sword':
            continue  # no transaction should exist for this invalid click
        captures = out / ('capture-' + name)
        captures.mkdir(exist_ok=True)
        x, y, offset = (109, 168, 0) if name == 'potion' else (109, 153, 6)
        seeded, expected = click('capture-' + name, seed, x, y, offset,
                                 extra=['--danu-capture', str(captures)], exe=args.probe)
        lines = (out / ('capture-' + name + '.log')).read_text(errors='replace')
        checkpoints = [(int(index), int(pc, 16)) for index, pc in re.findall(r'DANU-BOUNDARY index=(\d+) pc=([0-9a-f]+)', lines)]
        assert len(checkpoints) > 25, checkpoints
        for index, pc in checkpoints:
            saved = (captures / f'mid-{index:02}.sav').read_bytes()
            resumed = run(f'resume-{name}-{index:02}', saved, 1250)
            assert persistent(resumed) == persistent(expected), (name, index, hex(pc))
            boundaries += 1
    print(f'PASS: {boundaries} cold native transaction/sound/ritual save boundaries; no lost or repeated item/life changes', flush=True)

    report = {'native_cases': len(rows), 'exact_retail_results': compared,
              'cold_boundaries': boundaries, 'runs': reports,
              'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest()}
    (out / 'verification.json').write_text(json.dumps(report, indent=2))
    print(f'PASS: {len(reports)} production/capture replays; report: {out / "verification.json"}', flush=True)


if __name__ == '__main__':
    main()
