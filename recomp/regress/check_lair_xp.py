#!/usr/bin/env python3
"""Verify once-per-lair XP against retail and through real save/load paths.

Build lair_xp_probe.c separately; it is test-only and must never be deployed.
Local original game images are read-only. Every run uses a scratch --log.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OFFSET, SIZE, TAIL = 104, 0x200000, 76
SIDE = {2: 0, 3: 1, 4: 2, 5: 26}
STOP, STACK, ACTOR, NODES, ITEMS = 0x1ef000, 0x1ff000, 0x100000, 0x100800, 0x101000


def get(ram, address, size=4):
    return int.from_bytes(ram[address:address+size], 'big')


def put(ram, address, value, size=4):
    ram[address:address+size] = (value & ((1 << (8*size))-1)).to_bytes(size, 'big')


def versioned(source, version, history=bytes(24)):
    result = bytearray(source)
    old = struct.unpack_from('<I', result, 8)[0]
    off = len(result)-TAIL-SIDE[old]
    prior = result[off:off+SIDE[old]]
    fields = (prior+bytes(2))[:2]+history
    result[off:off+SIDE[old]] = fields[:SIDE[version]]
    struct.pack_into('<I', result, 8, version)
    return result


def machine(source, pc=STOP, player=0, node=0, prepare=True):
    result = bytearray(source)
    ram = bytearray(result[OFFSET:OFFSET+SIZE])
    if prepare:
        ram[ACTOR:ACTOR+0x2000] = bytes(0x2000)
        put(ram, 0x2dfda, NODES)
        for n in range(24):
            put(ram, NODES+n*20, ITEMS+n*24)
            put(ram, NODES+n*20+4, 0x0024000e)
            put(ram, NODES+n*20+8, 23, 2)
            put(ram, NODES+n*20+10, 0x005000b8)
    put(ram, 0x37178, NODES+node*20)
    put(ram, 0x2ebd0, ACTOR+player*0x84)
    put(ram, STACK, STOP)
    put(ram, STOP, 0x60fe, 2)
    regs = [0]*21
    regs[15] = regs[19] = STACK
    regs[16], regs[17] = pc, 0x2700
    struct.pack_into('<21I', result, 20, *regs)
    result[OFFSET:OFFSET+SIZE] = ram
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    parser.add_argument('--probe', type=Path, default=ROOT/'recomp/build/lair_xp_probe.exe')
    parser.add_argument('--before', type=Path)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=ROOT/'dist/MoonstoneNative/moonstone_combatrun.sav')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT/'dist/MoonstoneNative/data'
    source = args.fixture.read_bytes()

    def run(name, save, flags=(), exe=None):
        path = out/(name+'.sav')
        path.write_bytes(save)
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data/'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                   '--frames', '2', '--log', str(out/(name+'.log')),
                   '--savestate-at', '1', str(out/(name+'.end.sav')), *flags]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (out/(name+'.stdout')).write_text(result.stdout+result.stderr)
        assert result.returncode == 0, (name, result.stdout[-6000:], result.stderr[-6000:])
        final = (out/(name+'.end.sav')).read_bytes()
        assert struct.unpack_from('<I', final, 84)[0] == STOP, (name, 'did not finish')
        return bytearray(final), result.stdout

    current, _ = run('current', machine(source))
    assert struct.unpack_from('<I', current, 8)[0] == 5
    assert len(current) == len(source)+26-SIDE[struct.unpack_from('<I', source, 8)[0]]
    history = versioned(current, 5, bytes(i % 2 for i in range(24)))
    (out/'history.sav').write_bytes(history)
    for version in (2, 3, 4):
        (out/f'legacy-{version}.sav').write_bytes(versioned(current, version))
    for name, offset in [('invalid-first', -TAIL-24), ('invalid-last', -TAIL-1),
                         ('invalid-sword', -TAIL-26), ('invalid-ko', -TAIL-25)]:
        bad = bytearray(history)
        bad[offset] = 2
        (out/(name+'.sav')).write_bytes(bad)
    (out/'truncated.sav').write_bytes(history[:-1])
    (out/'trailing.sav').write_bytes(history+b'X')
    bad = bytearray(history)
    bad[-TAIL:-TAIL+64] = b'nonexistent-lair-test-file\0'.ljust(64, b'\0')
    struct.pack_into('<ii', bad, len(bad)-12, 0, 1)
    (out/'invalid-streamer.sav').write_bytes(bad)
    _, stdout = run('native-tests', machine(current),
                    ['--lair-tests', str(out), str(args.retail.resolve())], args.probe)
    print(stdout, flush=True)

    # Use the production executable and restart the process after EVERY reward.
    # The terminal loop only replaces the treasure UI at its existing boundary.
    def award(name, state, player=0, node=0, flags=(), exe=None):
        save = machine(state, 0x21cf4, player, node, prepare=False)
        ram = bytearray(save[OFFSET:OFFSET+SIZE])
        put(ram, 0x21d02, 0x4ef9, 2)
        put(ram, 0x21d04, STOP)
        save[OFFSET:OFFSET+SIZE] = ram
        return run(name, save, flags, exe)[0]

    state = current
    for visit in range(12):
        state = award(f'cold-visit-{visit}', state, visit % 4)
        ram = state[OFFSET:OFFSET+SIZE]
        assert [get(ram, ACTOR+i*0x84+0x4e, 2) for i in range(4)] == [1, 0, 0, 0]
        assert state[-TAIL-24:-TAIL] == bytes([1])+bytes(23)
    state = award('different-lair', state, 3, 23)
    assert get(state[OFFSET:OFFSET+SIZE], ACTOR+3*0x84+0x4e, 2) == 1
    print('PASS: 12 production cold reloads share one reward; a different lair still awards XP', flush=True)

    for pc in (0x21cf4, 0x21cfa, 0x21d00):
        saved = (out/f'boundary-{pc:x}.sav').read_bytes()
        ram = bytearray(saved[OFFSET:OFFSET+SIZE])
        put(ram, 0x21d02, 0x4ef9, 2)
        put(ram, 0x21d04, STOP)
        save = bytearray(saved)
        save[OFFSET:OFFSET+SIZE] = ram
        final, _ = run(f'cold-boundary-{pc:x}', save)
        assert get(final[OFFSET:OFFSET+SIZE], ACTOR+0x4e, 2) == 1
        assert final[-TAIL-24+7] == 1
    print('PASS: production cold resume before/during/after the award gives exactly one XP', flush=True)

    old = versioned(current, 4)
    values = []
    for name, flags, exe in [('disabled', ['--nolairxpfix'], args.exe),
                             ('baseline', [], args.before)]:
        if exe is None:
            continue
        state = old
        for visit in range(3):
            state = award(f'{name}-{visit}', state, flags=flags, exe=exe)
            assert get(state[OFFSET:OFFSET+SIZE], ACTOR+0x4e, 2) == visit+1
        values.append(state[OFFSET:OFFSET+SIZE])
    if len(values) == 2:
        assert values[0] == values[1]
    print('PASS: isolated A/B reproduces repeat XP and matches previous guest RAM', flush=True)
    (out/'results.json').write_text(json.dumps({'retail_comparisons': 576,
        'loot_cleanup_cases': 52, 'cold_repeat_visits': 12, 'award_save_boundaries': 3,
        'scope_guards': 16, 'failed_load_cases': 8, 'passed': True}, indent=2))


if __name__ == '__main__':
    main()
