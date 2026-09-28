#!/usr/bin/env python3
"""Compare sword generation/loot with original retail instructions and real UI.

Local retail RAM and a full pre-draw duel loot save are required, read-only.
All generated saves/logs stay in the requested scratch output directory.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess

from check_manual_armour import ROOT, RAM_OFFSET, RAM_SIZE, get, put, saved_ram

W, L, WI, LI = 0x100000, 0x100084, 0x100300, 0x100330
STOP, STACK = 0x1ef000, 0x1ff000
# Windows save streamer tail: char[64], two 32-bit longs, one 32-bit int.
STREAM_TAIL = 76


def sword_flag_offset(save):
    version = struct.unpack_from('<I', save, 8)[0]
    assert version in (3, 4, 5)
    return -STREAM_TAIL - {3: 1, 4: 2, 5: 26}[version]


def sword_flag(save):
    return save[sword_flag_offset(save)]


def machine(save, ram, pc, regs=None):
    save = bytearray(save)
    ram = bytearray(ram)
    put(ram, STOP, 0x60fe, 2)
    put(ram, STACK, STOP)
    values = [0] * 21
    values[15] = values[19] = STACK
    values[16], values[17] = pc, 0x2700
    for index, value in (regs or {}).items():
        values[index] = value
    struct.pack_into('<21I', save, 20, *values)
    save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
    return save


def actor(ram, address, items, weapon=0x16):
    ram[address:address + 0x84] = bytes(0x84)
    put(ram, address + 0x60, items)
    put(ram, address + 0x58, weapon)
    put(ram, address + 0x5c, 0x1b)
    put(ram, address + 0x47, 1, 1)
    put(ram, address + 0x49, 4, 1)
    put(ram, address + 0x50, 20, 2)


def world_swords(ram, retail=False):
    dragon, temple, nodes, stride = ((0x2e8a8, 0x2f8d2, 0x2dd6e, 0x16) if retail
                                     else (0x2ead0, 0x2fb20, 0x2df96, 0x14))
    counts = {'dragon': get(ram, dragon + 4, 1), 'temple': get(ram, temple + 4, 1)}
    base = get(ram, nodes + 0x44)
    for n in range(24):
        counts[f'lair{n}'] = get(ram, get(ram, base + n * stride) + 4, 1)
    return {name: value for name, value in counts.items() if value}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--retail', type=Path, required=True)
    ap.add_argument('--before-exe', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    original_save = args.fixture.resolve().read_bytes()
    original = saved_ram(original_save)
    reference = saved_ram(args.retail.resolve().read_bytes())
    data = ROOT / 'dist/MoonstoneNative/data'
    reports = []

    def run(name, save, *, frames=3, extra=(), snapshot=2, exe=None):
        inp, checkpoint = out / (name + '.sav'), out / (name + '-after.sav')
        inp.write_bytes(save)
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data / 'nb'),
                   '--diskdir', str(data), '--dataset', str(data), '--loadstate', str(inp),
                   '--frames', str(frames), '--log', str(out / (name + '.log')),
                   '--savestate-at', str(snapshot), str(checkpoint),
                   '--dumpram', str(out / (name + '.ram')), *extra]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (out / (name + '.stdout')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, name + ': ' + result.stderr
        assert checkpoint.exists(), name + ': no completed save'
        return bytearray(checkpoint.read_bytes()), (out / (name + '.ram')).read_bytes(), \
            (out / (name + '.log')).read_text(errors='replace')

    def routine(name, ram, pc, regs=None, state=original_save, extra=()):
        save, result, log = run(name, machine(state, ram, pc, regs), extra=extra)
        assert struct.unpack_from('<I', save, 20 + 16 * 4)[0] == STOP, name + ': no normal return'
        return save, bytearray(result), log

    blank, _, _ = routine('v3-empty', original, STOP)
    sizes = {2: 0, 3: 1, 4: 2, 5: 26}
    added = sizes[struct.unpack_from('<I', blank, 8)[0]] - sizes[struct.unpack_from('<I', original_save, 8)[0]]
    assert len(blank) == len(original_save) + added and sword_flag(blank) == 0

    # Complete original world constructors; no reward/drawing/sound stubs.
    seeds = (0xfffffde2, 0xc88f, 0xfffffffb, 0xacfb, 0x12345678, 0xabcdef01,
             0x9e3779b9, 0x31415926, 0xdeadbeef, 0x7fffffff, 0x13579bdf, 0x2468ace0)
    current_worlds = []
    for retail in (False, True):
        for seed in seeds:
            ram = bytearray(reference if retail else original)
            put(ram, 0x38f68 if retail else 0x391a8, seed)
            state, ram, _ = routine(f'world-{retail}-{seed:x}', ram,
                                    0x25c3c if retail else 0x25b4c,
                                    extra=['--noretailparity'] if retail else [])
            counts = world_swords(ram, retail)
            flag = get(ram, 0x2e8c6, 2) if retail else sword_flag(state)
            assert sum(counts.values()) <= 1 and flag == bool(counts), (retail, seed, counts, flag)
            if not retail:
                current_worlds.append(state)
            reports.append({'world_retail': retail, 'seed': seed, 'swords': counts})
    print('PASS: 12 current and 12 retail complete world setups each generate at most one sword', flush=True)

    # Execute the shared generator repeatedly, retaining serialized state each
    # time; force the SAME candidate roll across all recipient types.
    def gift(name, state, retail=False, mode=0, recipient=W, seed=0xacfb, extra=()):
        ram = saved_ram(state)
        put(ram, 0x38f68 if retail else 0x391a8, seed)
        put(ram, 0x37cc0 if retail else 0x37f00, 0)  # preceding prize was a potion
        put(ram, 0x2e9ac if retail else 0x2ebd0, recipient)
        put(ram, 0x36f28 if retail else 0x37178, 0x100500)
        return routine(name, ram, 0x2a95a if retail else 0x2aa9a, {3: mode}, state=state,
                       extra=([*extra, '--noretailparity'] if retail else extra))

    def empty_recipients(retail):
        ram = bytearray(reference if retail else original)
        ram[W:W + 0x800] = bytes(0x800)
        for n in range(4):
            actor(ram, W + n * 0x84, WI + n * 0x30)
        put(ram, 0x100500, 0x100540)
        stock = 0x2f8d2 if retail else 0x2fb20
        ram[stock:stock + 0x18] = bytes(0x18)
        if retail:
            put(ram, 0x2e8c6, 0, 2)
        return machine(blank, ram, STOP)

    def inventories(ram, retail):
        locations = [WI + n * 0x30 for n in range(4)] + [0x100540, 0x2f8d2 if retail else 0x2fb20]
        return [bytes(ram[a:a + 0x18]).hex() for a in locations]

    states = [empty_recipients(False), empty_recipients(True)]
    pre = bytearray(states[0])
    first = None
    for n in range(49):
        states[0], actual, _ = gift(f'gift-current-{n}', states[0], mode=n % 3, recipient=W + n % 4 * 0x84)
        states[1], expected, _ = gift(f'gift-retail-{n}', states[1], True, n % 3, W + n % 4 * 0x84)
        assert inventories(actual, False) == inventories(expected, True), f'gift{n}: retail inventory mismatch'
        assert sword_flag(states[0]) == get(expected, 0x2e8c6, 2) == 1
        if n == 0:
            first = bytearray(states[0])
    reports.append({'later_gifts': 48, 'all_inventories_match_retail': True})
    print('PASS: first sword and 48 subsequent gifts match retail across four characters, lair and temple', flush=True)

    # Same generator with only the correction disabled reproduces multiple copies.
    old, _, _ = gift('disabled-first', pre, extra=['--noswordfix'])
    old, oldram, _ = gift('disabled-second', old, recipient=L, extra=['--noswordfix'])
    assert (get(oldram, WI + 4, 1), get(oldram, LI + 4, 1)) == (1, 1)
    reports.append({'disabled_generation_swords': 2})

    # All first-destination modes can win the single sword; subsequent recipients
    # cannot create another, even when the first holder's count later reaches0.
    for first_mode, second_mode in itertools.product(range(3), repeat=2):
        state, ram, _ = gift(f'first-mode-{first_mode}-{second_mode}', pre, mode=first_mode)
        place = (WI, 0x100540, 0x2fb20)[first_mode]
        assert get(ram, place + 4, 1) == 1 and sword_flag(state) == 1
        put(ram, place + 4, 0, 1)
        state[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
        state, ram, _ = gift(f'next-mode-{first_mode}-{second_mode}', state, mode=second_mode, recipient=L)
        assert all(get(ram, address + 4, 1) == 0 for address in (WI, LI, 0x100540, 0x2fb20))
        assert sword_flag(state) == 1
    print('PASS: all nine destination orders share the latch; losing the sword cannot generate a replacement', flush=True)

    # A new campaign really resets the serialized latch (without resetting it
    # on every gift or menu visit). Compare the whole initialized guest RAM.
    for seed in (0xfffffde2, 0xacfb):
        ram = bytearray(original)
        put(ram, 0x391a8, seed)
        zero, zr, _ = routine(f'new-world-zero-{seed:x}', ram, 0x25b4c, state=pre)
        one, rr, _ = routine(f'new-world-one-{seed:x}', ram, 0x25b4c, state=first)
        assert zr == rr and sword_flag(zero) == sword_flag(one)
    print('PASS: new-world initialization clears prior campaign history; cold loads preserve new history', flush=True)

    # Preserve generated states for warm-load/invalid-load probe checks.
    (out / 'flag-zero.sav').write_bytes(pre)
    (out / 'flag-one.sav').write_bytes(first)
    bad = bytearray(first)
    bad[sword_flag_offset(bad)] = 2
    (out / 'invalid-flag.sav').write_bytes(bad)
    (out / 'truncated.sav').write_bytes(first[:-1])
    (out / 'trailing.sav').write_bytes(first + b'!')
    bad = bytearray(first)
    bad[-STREAM_TAIL:-STREAM_TAIL + 64] = b'not-a-real-sword-test-module\0'.ljust(64, b'\0')
    struct.pack_into('<ii', bad, len(bad) - 12, 0, 1)
    (out / 'invalid-streamer.sav').write_bytes(bad)

    # Real generated sword -> temple sale -> buyback. Execute original label,
    # slot and click logic; only sprite, redraw and feedback rendering are stubbed.
    shop_results = []
    for retail in (False, True):
        state, ram, _ = gift(f'shop-origin-{retail}', empty_recipients(retail), retail)
        stock = 0x2f8d2 if retail else 0x2fb20
        for address in ((0x2cc36, 0x2bb9a, 0x2c748) if retail else (0x3aa44, 0x2bca4, 0x2c85e, 0x3c110)):
            put(ram, address, 0x4e75, 2)
        put(ram, W + 0x4a, 50, 2)

        def shop(action, right, gold=None):
            nonlocal state, ram
            if gold is not None:
                put(ram, W + 0x4a, gold, 2)
            for address, value in ((0x2de94 if retail else 0x2e0bc, W),
                                   (0x2de98 if retail else 0x2e0c0, L),
                                   (0x2f8ca if retail else 0x2fb1c, 6)):
                put(ram, address, value)
            put(ram, 0x39092 if retail else 0x392d4, 200 if right else 80, 2)
            extra = ['--noretailparity'] if retail else []
            state, ram, _ = routine(f'shop-{retail}-{action}-labels', ram,
                                    0x2d4ba if retail else 0x2d6e0, state=state, extra=extra)
            state, ram, _ = routine(f'shop-{retail}-{action}-slot', ram,
                                    0x2c8d4 if retail else 0x2ca08, {8: stock if right else WI},
                                    state=state, extra=extra)
            kind = 0x2f8a6 if retail else 0x2faf6
            arg = 0x2f8ac if retail else 0x2fafc
            assert get(ram, kind, 2) == 1 and get(ram, arg, 2) == 4
            label = ((0x2fd70 if right else 0x2fbf6) if retail else
                     (0x2ffbe if right else 0x2fe44)) + 13 * 14
            put(ram, 0x100708, label)
            put(ram, 0x100710, 0x34)
            put(ram, 0x100714, 1, 2)
            put(ram, 0x100716, 4, 2)
            state, ram, _ = routine(f'shop-{retail}-{action}', ram,
                                    0x2cc4a if retail else 0x2cda6, {8: 0x100700}, state=state, extra=extra)
            state, ram, _ = routine(f'shop-{retail}-{action}-stats', ram, 0x21474, {8: W}, state=state, extra=extra)
            result = [get(ram, WI + 4, 1), get(ram, stock + 4, 1), get(ram, W + 0x4a, 2), get(ram, W + 0x58)]
            shop_results.append({'retail': retail, 'action': action, 'result': result})
            return result

        assert shop('sell', False) == [0, 1, 100, 0x16]
        assert shop('insufficient-gold', True, 99) == [0, 1, 99, 0x16]
        assert shop('buy', True, 100) == [1, 0, 0, 0x19]
        kind = 0x2f8a6 if retail else 0x2faf6
        put(ram, kind, 0xffff, 2)
        state, ram, _ = routine(f'shop-{retail}-empty-stock', ram,
                                0x2c8d4 if retail else 0x2ca08, {8: stock}, state=state,
                                extra=['--noretailparity'] if retail else [])
        assert get(ram, kind, 2) == 0xffff, 'Empty stock emitted a sword slot'
        assert (get(ram, 0x2e8c6, 2) if retail else sword_flag(state)) == 1
    reports.extend(shop_results)
    print('PASS: generated sword sale/buyback, insufficient funds and empty stock match retail', flush=True)

    # Retail transfer/stat oracle, drawing and sound alone isolated.
    def transfer(name, retail, winner_weapon, loser_weapon, scene=1, counts=None, extra=()):
        ram = bytearray(reference if retail else original)
        ram[W:W + 0x800] = bytes(0x800)
        actor(ram, W, WI, winner_weapon)
        actor(ram, L, LI, loser_weapon)
        counts = counts or (int(winner_weapon == 0x19), int(loser_weapon == 0x19))
        put(ram, WI + 4, counts[0], 1)
        put(ram, LI + 4, counts[1], 1)
        for offset, value in ((0, 2), (2, 1), (8, 3), (0x14, 1), (0x16, 2)):
            put(ram, WI + offset, value, 1)
            put(ram, LI + offset, value, 1)
        globals_ = (0x2f8b6, 0x2f8be, 0x2f8ba, 0x2f8c2, 0x2f8ca) if retail else \
                   (0x2fb08, 0x2fb10, 0x2fb0c, 0x2fb14, 0x2fb1c)
        for address, value in zip(globals_, (W, L, WI, LI, scene)):
            put(ram, address, value)
        counter = 0x2f8b2 if retail else 0x2fb02
        put(ram, counter, 0, 2)
        for address in ((0x2cc36, 0x2bb9a) if retail else (0x3aa44, 0x2bca4)):
            put(ram, address, 0x4e75, 2)
        special = loser_weapon == 0x19
        pc = (0x2cea8 if special else 0x2d03c) if retail else (0x2d014 if special else 0x2d1cc)
        state, after, log = routine(name, ram, pc, {0: 0x20, 1: 4, 8: W, 9: L},
                                    extra=[*extra, '--noretailparity'] if retail else extra)
        assert all(after[a + offset:a + offset + 1] == ram[a + offset:a + offset + 1]
                   for a in (WI, LI) for offset in range(0x18) if offset != 4), 'Other items changed'
        result = {'weapons': [get(after, a + 0x58) for a in (W, L)],
                  'counts': [get(after, a + 4, 1) for a in (WI, LI)],
                  'selected': get(after, counter, 2)}
        return result

    oracle = {}
    for ww, lw in itertools.product(range(0x16, 0x1a), repeat=2):
        if ww == lw == 0x19:
            continue  # two swords cannot be generated under the retail rule
        expected = transfer(f'take-retail-{ww:x}-{lw:x}', True, ww, lw)
        actual = transfer(f'take-current-{ww:x}-{lw:x}', False, ww, lw)
        assert actual == expected, (ww, lw, actual, expected)
        oracle[ww, lw] = expected
        reports.append({'transfer': [ww, lw], 'result': actual})
    for scene in (2, 8, 10):
        expected = transfer(f'item-retail-{scene}', True, 0x16, 0x19, scene)
        actual = transfer(f'item-current-{scene}', False, 0x16, 0x19, scene)
        assert actual == expected
    empty = transfer('empty-item-no-underflow', False, 0x16, 0x19, counts=(0, 0))
    assert empty['counts'] == [0, 0] and empty['selected'] == 0
    print('PASS: all 15 legal weapon pairs match retail; lair, acquisition, hoard and empty-source checks pass', flush=True)

    # Shipping executable, full original post-duel drawing/input/loot, no stubs.
    uw, ul = get(original, 0x2fb08), get(original, 0x2fb10)
    uwi, uli = get(original, uw + 0x60), get(original, ul + 0x60)
    for lives in (4, 0, 255):
        for (ww, lw), expected in oracle.items():
            save, ram = bytearray(original_save), bytearray(original)
            for a, items, weapon in ((uw, uwi, ww), (ul, uli, lw)):
                put(ram, a + 0x58, weapon)
                put(ram, items + 4, int(weapon == 0x19), 1)
            put(ram, ul + 0x49, lives, 1)
            put(ram, 0x392d4, 205, 2)
            put(ram, 0x392d6, 100, 2)
            save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
            checkpoint, after, log = run(f'ui-{lives}-{ww:x}-{lw:x}', save, frames=310, snapshot=249,
                                         extra=['--script', '190:f,198:.,260:f,268:.'])
            actual = {'weapons': [get(after, a + 0x58) for a in (uw, ul)],
                      'counts': [get(after, a + 4, 1) for a in (uwi, uli)]}
            assert actual == {k: expected[k] for k in actual}, (lives, ww, lw, actual, expected)
            assert get(after, 0x2fb1c) == (9 if lives == 4 and lw != 0x16 else 1)
            assert log.count('SWORD-LOOT ') == (lw == 0x19), 'Repeated or missing special transfer'
            assert log.count('WEAPON-LOOT ') == (lw in (0x17, 0x18)), 'Repeated or missing ordinary transfer'
            for items in (uwi, uli):
                assert all(ram[items + n] == after[items + n] for n in range(0x18) if n != 4)
            if lives == 0 and (ww, lw) == (0x16, 0x19):
                (out / 'loot-after.sav').write_bytes(checkpoint)
            reports.append({'ui': [lives, ww, lw], 'result': actual})
    print('PASS: 45 real UI cases, living/dead losers, repeated clicks and unrelated inventory preservation', flush=True)

    # Actual temple UI, including the corrected equipment-slot metadata on sale.
    save, ram = bytearray(original_save), bytearray(original)
    put(ram, 0x2fb1c, 6)
    put(ram, uw + 0x58, 0x16)
    put(ram, uwi + 4, 0, 1)
    put(ram, uw + 0x4a, 150, 2)
    ram[0x2fb20:0x2fb38] = bytes(0x18)
    put(ram, 0x2fb24, 1, 1)
    put(ram, 0x392d4, 205, 2)
    put(ram, 0x392d6, 100, 2)
    save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
    shop_save, after, _ = run('ui-temple-buy', save, frames=310, snapshot=300,
                              extra=['--script', '190:f,198:.,260:f,268:.'])
    assert [get(after, a, size) for a, size in ((uwi + 4, 1), (0x2fb24, 1), (uw + 0x4a, 2))] == [1, 0, 50]
    ram = saved_ram(shop_save)
    put(ram, 0x392d4, 55, 2)
    shop_save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
    _, after, _ = run('ui-temple-sell', shop_save, frames=130, snapshot=120,
                       extra=['--script', '10:f,18:.,70:f,78:.'])
    assert [get(after, a, size) for a, size in ((uwi + 4, 1), (0x2fb24, 1), (uw + 0x4a, 2))] == [0, 1, 100]
    assert get(after, uw + 0x58) == 0x16
    reports.append({'real_temple_ui': 'buy/sell/repeated clicks conserve one sword and correct gold'})
    print('PASS: real temple UI buy/sell and repeated empty-slot clicks preserve sword and gold', flush=True)

    # Both historical failures must still reproduce with the focused A/B flag.
    for ww, lw, expected_counts in ((0x19, 0x16, [2, 255]), (0x16, 0x19, [0, 1])):
        ram = bytearray(original)
        for a, items, weapon in ((uw, uwi, ww), (ul, uli, lw)):
            put(ram, a + 0x58, weapon)
            put(ram, items + 4, int(weapon == 0x19), 1)
        put(ram, 0x392d4, 205, 2)
        put(ram, 0x392d6, 100, 2)
        save = bytearray(original_save)
        save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
        _, after, _ = run(f'ui-disabled-{ww:x}-{lw:x}', save, frames=230, snapshot=220,
                           extra=['--script', '190:f,198:.', '--noswordfix'])
        assert [get(after, a + 4, 1) for a in (uwi, uli)] == expected_counts
        if args.before_exe:
            _, before, _ = run(f'ui-before-{ww:x}-{lw:x}', save, frames=230, snapshot=220,
                                exe=args.before_exe, extra=['--script', '190:f,198:.'])
            assert after == before, 'A/B must preserve the pre-fix behavior'
    print('PASS: disabling only the sword fix reproduces both previous UI failures', flush=True)

    (out / 'results.json').write_text(json.dumps({'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        'fixture_sha256': hashlib.sha256(original_save).hexdigest(), 'cases': reports}, indent=2))


if __name__ == '__main__':
    main()
