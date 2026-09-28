#!/usr/bin/env python3
"""Compare manual armour loot with retail instructions and replay the real UI.

Requires local, uncommitted retail RAM and a full campaign-duel loot save taken
before the inventory is drawn. No original save, disk, setting or log is written.
The routine oracle stubs only sound and drawing: the retail transfer, selection
counter, maximum-HP cap and movement calculation all execute as original 68000
instructions. UI cases use the shipping executable without those stubs.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RAM_OFFSET, RAM_SIZE = 104, 0x200000
WINNER, LOSER = 0x100000, 0x100084
WIN_ITEMS, LOSE_ITEMS = 0x100200, 0x100230


def get(ram, address, size=4):
    return int.from_bytes(ram[address:address + size], 'big')


def put(ram, address, value, size=4):
    ram[address:address + size] = (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'big')


def saved_ram(save):
    assert len(save) >= RAM_OFFSET + RAM_SIZE, 'Full save required'
    return bytearray(save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE])


def actor_state(ram, actor):
    return {name: get(ram, actor + offset, size) for name, offset, size in (
        ('hp', 0x50, 2), ('max_hp', 0x54, 2), ('moves', 0x56, 1),
        ('armour', 0x5c, 4), ('weapon', 0x58, 4), ('lives', 0x49, 1),
        ('gold', 0x4a, 2), ('xp', 0x4e, 2))}


def seed_actor(ram, actor, items, armour, con, rings, missing_hp, lives=4):
    for offset, value in ((0x46, 2), (0x47, con), (0x48, 2), (0x49, lives), (0x4c, 3)):
        put(ram, actor + offset, value, 1)
    put(ram, actor + 0x4a, 37, 2)
    put(ram, actor + 0x4e, 2, 2)
    put(ram, actor + 0x58, 0x16)
    put(ram, actor + 0x5c, armour)
    put(ram, actor + 0x60, items)
    maximum = (con + 1) * 10 + rings * 20 + (armour - 0x1b) * 10
    put(ram, actor + 0x50, maximum - missing_hp, 2)
    put(ram, actor + 0x54, maximum, 2)
    put(ram, actor + 0x56, 8 + (2 if armour in (0x1c, 0x1e) else 0), 1)
    ram[items:items + 0x24] = bytes(0x24)
    for offset, value in ((0, 2), (2, 1), (6, rings), (8, 2), (0x14, 1), (0x16, 2)):
        put(ram, items + offset, value, 1)


def unchanged_items(before, after, actors):
    for actor in actors:
        items = get(before, actor + 0x60)
        assert before[items:items + 0x24] == after[items:items + 0x24], 'Other loot changed'
        assert before[actor + 0x46:actor + 0x50] == after[actor + 0x46:actor + 0x50], \
            'Stats, lives, gold, daggers or XP changed'
        assert get(before, actor + 0x58) == get(after, actor + 0x58), 'Weapon changed'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--retail', type=Path, required=True)
    ap.add_argument('--before-exe', type=Path, help='Optional frozen pre-fix executable')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    source = args.fixture.resolve().read_bytes()
    original = saved_ram(source)
    retail = saved_ram(args.retail.resolve().read_bytes())
    data = ROOT / 'dist/MoonstoneNative/data'
    reports = []
    with tempfile.TemporaryDirectory(prefix='moon-armour-') as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)

        def run(name, save, frames=1, extra=(), exe=None):
            path = out / (name + '.sav')
            path.write_bytes(save)
            command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data / 'nb'),
                       '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                       '--frames', str(frames), '--log', str(out / (name + '.log')),
                       '--dumpram', str(out / (name + '.ram')), *extra]
            result = subprocess.run(command, capture_output=True, text=True, timeout=40)
            assert result.returncode == 0, f'{name}: {result.stdout}\n{result.stderr}'
            log = (out / (name + '.log')).read_text(errors='replace')
            return (out / (name + '.ram')).read_bytes(), log

        def routine(name, reference, warm, larm, profile, extra=(), exe=None, entry=None):
            save = bytearray(source)
            ram = bytearray(retail if reference else original)
            ram[WINNER:WINNER + 0x300] = bytes(0x300)
            con, rings, missing = profile
            seed_actor(ram, WINNER, WIN_ITEMS, warm, con, rings, missing)
            seed_actor(ram, LOSER, LOSE_ITEMS, larm, con, rings, 0)
            winner_global, loser_global, counter = ((0x2f8b6, 0x2f8be, 0x2f8b2)
                                                     if reference else (0x2fb08, 0x2fb10, 0x2fb02))
            put(ram, winner_global, WINNER)
            put(ram, loser_global, LOSER)
            put(ram, counter, 0, 2)
            # Transparent sound and screen drawing are isolated from guest rules.
            put(ram, 0x2cc36 if reference else 0x3aa44, 0x4e75, 2)
            put(ram, 0x2bb9a if reference else 0x2bca4, 0x4e75, 2)
            put(ram, 0x1ff000, 0x1ef000)
            put(ram, 0x1ef000, 0x60fe, 2)
            regs = [0] * 21
            regs[8], regs[9] = WINNER, LOSER
            regs[15] = regs[19] = 0x1ff000  # A7 and active supervisor stack
            regs[16], regs[17] = (0x2cff2 if reference else 0x2d184), 0x2700
            if entry is not None:
                regs[16] = entry
            struct.pack_into('<21I', save, 20, *regs)
            save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
            result, log = run(name, save, extra=extra, exe=exe)
            state = {'winner': actor_state(result, WINNER), 'loser': actor_state(result, LOSER),
                     'selected': get(result, counter, 2)}
            return state, ram, result, log

        # Exhaust all 16 pairs with full health, injured winners, and CON/rings.
        profiles = [(1, 0, 0), (1, 0, 13), (4, 3, 37), (4, 1, 4)]
        oracle = {}
        for p, profile in enumerate(profiles):
            for warm, larm in itertools.product(range(0x1b, 0x1f), repeat=2):
                name = f'p{p}-{warm:02x}-{larm:02x}'
                expected, _, _, _ = routine('retail-' + name, True, warm, larm, profile,
                                             extra=['--noretailparity'])
                actual, before, after, log = routine('current-' + name, False, warm, larm, profile)
                assert actual == expected, f'{name}: {actual} != retail {expected}'
                unchanged_items(before, after, (WINNER, LOSER))
                assert log.count('SFX-CLICK site=02d1ac') == (larm != 0x1b), name + ': sound count'
                assert actual['winner']['hp'] <= actual['winner']['max_hp'], name + ': overheal'
                assert actual['selected'] == (larm != 0x1b), name + ': selection count'
                oracle[p, warm, larm] = expected
                reports.append({'case': name, 'retail': expected, 'current': actual})
        print('PASS: 64 combinations match executed retail transfer/stat instructions; other loot unchanged', flush=True)

        old, _, _, crash_log = routine('disabled-crash', False, 0x1c, 0x1e, profiles[0],
                                       extra=['--nomanualarmorfix'])
        fixed = oracle[0, 0x1c, 0x1e]
        assert old != fixed and old['selected'] == 0, 'A/B did not reproduce old failure'
        assert '02d1a0 000c5a' in crash_log, 'A/B must reach the original address-error vector'
        old_hp, _, _, _ = routine('disabled-hp', False, 0x1b, 0x1e, profiles[0],
                                  extra=['--nomanualarmorfix'])
        assert old_hp['winner']['hp'] == 20 and old_hp['loser']['hp'] == 50
        # Disabled parity is byte-identical to the pre-fix build on these paths.
        for warm, larm in ((0x1b, 0x1e), (0x1c, 0x1e), (0x1e, 0x1c)):
            name = f'baseline-{warm:02x}-{larm:02x}'
            _, _, disabled, _ = routine(name, False, warm, larm, profiles[0],
                                         extra=['--noretailparity'])
            if args.before_exe:
                _, _, previous, _ = routine('before-' + name, False, warm, larm, profiles[0],
                                             extra=['--noretailparity'], exe=args.before_exe)
                assert previous == disabled, name + ': disabled-parity RAM drift'
        print('PASS: disabled-fix comparisons reproduce the crash/HP defect; parity-off baseline retained', flush=True)

        silent, _, _, log = routine('without-sfx', False, 0x1c, 0x1e, profiles[0],
                                    extra=['--noretailsfx'])
        assert silent == fixed and 'SFX-CLICK' not in log, 'Sound switch changed the transfer'
        for warm, larm in ((0x1a, 0x1e), (0x1f, 0x1e), (0x1b, 0x1a), (0x1b, 0x1f)):
            state, before, after, _ = routine(f'invalid-{warm:x}-{larm:x}', False, warm, larm, profiles[0])
            assert state['selected'] == 0
            assert before[WINNER:WINNER + 0x300] == after[WINNER:WINNER + 0x300], 'Invalid gear mutated actors'
        if args.before_exe:
            for warm, larm in itertools.product(range(0x1b, 0x1f), repeat=2):
                name = f'automatic-{warm:02x}-{larm:02x}'
                _, _, after, _ = routine(name, False, warm, larm, profiles[0], entry=0x21576)
                _, _, before, _ = routine('before-' + name, False, warm, larm, profiles[0],
                                           entry=0x21576, exe=args.before_exe)
                assert before == after, name + ': separate automatic loot path changed'
            print('PASS: all 16 automatic armour/gold outcomes remain byte-identical to the previous build', flush=True)
        print('PASS: sound-disabled transfer and malformed armour no-op guards', flush=True)

        # Real campaign UI: draw fresh hotspots, click armour, repeat the click.
        ui_winner, ui_loser = get(original, 0x2fb08), get(original, 0x2fb10)
        assert (ui_winner, ui_loser) == (0x2e7dc, 0x2e8e4), 'Use documented duel fixture'
        count = 0
        reload_source = None
        for lives, p in itertools.product((4, 0, 255), (0, 2)):
            for warm, larm in itertools.product(range(0x1b, 0x1f), repeat=2):
                name = f'ui-l{lives}-p{p}-{warm:02x}-{larm:02x}'
                save = bytearray(source)
                ram = saved_ram(save)
                con, rings, missing = profiles[p]
                seed_actor(ram, ui_winner, get(ram, ui_winner + 0x60), warm, con, rings, missing)
                seed_actor(ram, ui_loser, get(ram, ui_loser + 0x60), larm, con, rings, 0, lives)
                put(ram, 0x392d4, 200, 2)
                put(ram, 0x392d6, 125, 2)
                save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
                checkpoint = out / (name + '-after.sav')
                extra = ['--script', '190:f,198:.,260:f,268:.',
                         '--savestate-at', '249', str(checkpoint)]
                if warm == 0x1c and larm == 0x1e and lives == 4 and p == 0:
                    extra += ['--dump', str(out / 'armour-fixed'), '--dumpevery', '249']
                    reload_source = checkpoint
                after, log = run(name, save, frames=310, extra=extra)
                expected = oracle[p, warm, larm]
                assert actor_state(after, ui_winner) == expected['winner'], name + ': winner'
                loser = actor_state(after, ui_loser)
                want_loser = dict(expected['loser'], lives=lives)
                if lives != 4 and larm != 0x1b:
                    # Dead loser's inventory is redrawn and normalized too.
                    want_loser['max_hp'] = (con + 1) * 10 + rings * 20
                    want_loser['moves'] = 8
                assert loser == want_loser, f'{name}: loser {loser} != {want_loser}'
                assert get(after, 0x2fb1c) == (9 if lives == 4 and larm != 0x1b else 1), name + ': scene'
                assert get(after, 0x2fb02, 2) == (lives == 4 and larm != 0x1b), name + ': picks'
                assert log.count('ARMOR-MANUAL ') == (larm != 0x1b), name + ': repeated transfer'
                assert log.count('SFX-CLICK site=02d1ac') == (larm != 0x1b), name + ': repeated sound'
                unchanged_items(ram, after, (ui_winner, ui_loser))
                reports.append({'case': name, 'winner': actor_state(after, ui_winner), 'loser': loser})
                count += 1
        print(f'PASS: {count} real UI cases; alive/dead losers, one-pick limits, repeat clicks, rings and health', flush=True)

        assert reload_source is not None
        checkpoint = reload_source.read_bytes()
        before = saved_ram(checkpoint)
        after, log = run('reload-after-transfer', checkpoint, frames=90,
                         extra=['--script', '10:f,18:.'])
        for actor in (ui_winner, ui_loser):
            assert actor_state(before, actor) == actor_state(after, actor), 'Reload changed loot/health'
        unchanged_items(before, after, (ui_winner, ui_loser))
        assert 'ARMOR-MANUAL ' not in log, 'Reload repeated the transfer'
        print('PASS: existing pre-loot saves and post-transfer reload; no repeated grant', flush=True)

        manifest = {'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                    'fixture_sha256': hashlib.sha256(source).hexdigest(),
                    'retail_sha256': hashlib.sha256(args.retail.read_bytes()).hexdigest(),
                    'cases': reports}
        (out / 'results.json').write_text(json.dumps(manifest, indent=2))
        print(f'PASS: manual armour validation complete ({len(reports)} matrix/UI cases)', flush=True)


if __name__ == '__main__':
    main()
