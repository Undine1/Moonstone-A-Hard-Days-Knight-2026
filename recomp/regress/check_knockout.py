#!/usr/bin/env python3
"""Execute original retail rat-kill/reaction routines and verify host save state.

Local copyrighted specimens are read only. Generated saves, logs and captures
stay in --output or a disposable directory. Build knockout_probe.c separately
for instruction-boundary save and warm-load checks; never deploy that probe.
"""
import argparse
import itertools
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OFFSET, SIZE, TAIL = 104, 0x200000, 76  # Windows streamer tail: name[64] + 3 longs
HUMAN, RAT, NEXT = 0x100000, 0x100084, 0x100108
STOP, STACK = 0x1ef000, 0x1ff000


def get(ram, a, n=4):
    return int.from_bytes(ram[a:a+n], 'big')


def put(ram, a, value, n=4):
    ram[a:a+n] = (value & ((1 << (8*n))-1)).to_bytes(n, 'big')


def versioned(source, version=4, latch=0):
    result = bytearray(source)
    old = struct.unpack_from('<I', result, 8)[0]
    count = {2: 0, 3: 1, 4: 2, 5: 26}[old]
    off = len(result)-TAIL-count
    sword = result[off] if count else 0
    flags = bytes([sword, latch][:max(0, version-2)])
    if version >= 5:
        flags += bytes(24)
    result[off:off+count] = flags
    struct.pack_into('<I', result, 8, version)
    return result


def ko_offset(save):
    return -TAIL - (25 if struct.unpack_from('<I', save, 8)[0] >= 5 else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/knockout_probe.exe')
    ap.add_argument('--fixture', type=Path, default=ROOT/'dist/MoonstoneNative/moonstone_combatrun.sav')
    ap.add_argument('--retail', type=Path, required=True)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    source = args.fixture.read_bytes()
    retail = args.retail.read_bytes()[OFFSET:OFFSET+SIZE]
    current = source[OFFSET:OFFSET+SIZE]
    data = ROOT/'dist/MoonstoneNative/data'
    reports = []
    with tempfile.TemporaryDirectory(prefix='moon-ko-') as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)

        def run(name, save, extra=(), exe=None, frames=2):
            path = out/(name+'.sav')
            path.write_bytes(save)
            command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data/'nb'),
                       '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                       '--frames', str(frames), '--log', str(out/(name+'.log')),
                       '--dumpram', str(out/(name+'.ram')), '--savestate-at', '1',
                       str(out/(name+'.end.sav')), *extra]
            p = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (out/(name+'.stdout')).write_text(p.stdout+p.stderr)
            assert p.returncode == 0, f'{name}: {p.stdout}\n{p.stderr}'
            final = (out/(name+'.end.sav')).read_bytes()
            assert struct.unpack_from('<I', final, 20+16*4)[0] == STOP, name+': routine did not return'
            return (out/(name+'.ram')).read_bytes(), final[ko_offset(final)]

        def prepare(reference=False, hp=5, mutual=False, gore=True, facing=1,
                    latch=1, entry=None, duplicate=False):
            ram = bytearray(retail if reference else current)
            ram[HUMAN:HUMAN+0x2000] = bytes(0x2000)
            pair = 0x2de94 if reference else 0x2e0bc
            display = 0x2ea68 if reference else 0x2eca0
            global_actor = 0x2e9ac if reference else 0x2ebd0
            global_target = 0x2e9b0 if reference else 0x2ebd4
            put(ram, pair, HUMAN); put(ram, pair+4, RAT)
            put(ram, pair+8, 1, 1); put(ram, pair+0x10, 0, 1)
            put(ram, 0x302cc if reference else 0x30518, 0 if gore else 1)
            put(ram, global_actor, HUMAN); put(ram, global_target, HUMAN)
            put(ram, 0x2dfce if reference else 0x2e1f6, HUMAN)
            put(ram, 0x2e8c8, latch, 2)
            # Retail reference was captured at the menu, before new-world
            # initialization. This is the kind-0 table assignment made by its
            # original 0x268f0 initializer (current equivalent 0x267ba).
            put(ram, 0x2e8de if reference else 0x2eb02, 0x2681c if reference else 0x266f4)
            for actor, kind in ((HUMAN, 0x0c), (RAT, 0), (NEXT, 0x0c)):
                put(ram, actor, 1); put(ram, actor+4, 120, 2)
                put(ram, actor+8, 110, 2); put(ram, actor+0xa, 1, 1)
                put(ram, actor+0xb, 2, 1); put(ram, actor+0x4d, kind, 1)
                put(ram, actor+0x50, 10, 2); put(ram, actor+0x54, 20, 2)
                put(ram, actor+0x49, 4, 1)
                put(ram, actor+0x16, 0x111111); put(ram, actor+0x1a, 0x123456)
                put(ram, actor+0x1e, 0x101a00); put(ram, actor+0x2a, 0x101b00)
                put(ram, actor+0x60, 0x101c00)
                put(ram, actor+0x58, 0x16)
            for i in range(0, 0x44, 4):
                put(ram, 0x101a00+i, 0x234560+i); put(ram, 0x101b00+i, 7)
            put(ram, HUMAN+0x50, hp, 2); put(ram, HUMAN+0x12, RAT)
            put(ram, HUMAN+0xe, RAT if mutual else 0)
            put(ram, RAT+0xa, facing, 1); put(ram, RAT+0xe, HUMAN)
            ram[display:display+10*0x32] = bytes(10*0x32)
            for i in range(10): put(ram, display+i*0x32+0x24, 0x101000+i*0x40)
            for i, actor in ((3, HUMAN), (4, RAT)):
                put(ram, display+i*0x32, 1, 1)
                put(ram, display+i*0x32+0x18, actor)
                put(ram, display+i*0x32+2, 0x345678)
            if duplicate:
                put(ram, display+0x32+0x18, RAT)
                put(ram, display+0x32+2, 0xabcdef)
            put(ram, STOP, 0x60fe, 2); put(ram, STACK, STOP)
            regs = [0]*21
            regs[8] = regs[9] = HUMAN
            regs[15] = regs[19] = STACK
            regs[16], regs[17] = entry or (0x26230 if reference else 0x26118), 0x2700
            save = versioned(source, latch=latch)
            save[OFFSET:OFFSET+SIZE] = ram
            struct.pack_into('<21I', save, 20, *regs)
            return save

        def state(ram, host_latch, reference=False):
            pair = 0x2de94 if reference else 0x2e0bc
            d = (0x2ea68 if reference else 0x2eca0)+4*0x32
            anim = get(ram, 0x2e8d2 if reference else 0x2eaf8)
            script = get(ram, d+2)
            mapping = {0x335d6: 0x33826, 0x33762: 0x339b2,
                       0x336ba: 0x3390a, 0x3370e: 0x3395e}
            return {'hp': get(ram, HUMAN+0x50, 2), 'rat_hp': get(ram, RAT+0x50, 2),
                    'latch': get(ram, 0x2e8c8, 2) if reference else host_latch,
                    'round': get(ram, pair+8, 1), 'countdown': get(ram, pair+0x10, 1),
                    'animation': mapping.get(anim, anim), 'rat_script': mapping.get(script, script),
                    'positioned': get(ram, d+1, 1), 'workspace': ram[0x101100:0x101124].hex()}

        # Actual human handler -> kind-0 attack dispatcher -> original death
        # install and round helpers. No gameplay helpers are stubbed.
        for hp, mutual, gore, facing in itertools.product((-1, 0, 1, 5, 6, 20), (False, True), (False, True), (1, 3)):
            name = f'h{hp}-m{int(mutual)}-g{int(gore)}-f{facing}'
            values = []
            for reference in (True, False):
                save = prepare(reference, hp, mutual, gore, facing)
                ram, flag = run(('retail-' if reference else 'current-')+name, save,
                                ['--noretailparity'] if reference else [])
                values.append(state(ram, flag, reference))
            assert values[0] == values[1], f'{name}: {values}'
            assert values[1]['latch'] == int(gore and hp <= 5), name
            reports.append({'case': name, 'retail': values[0], 'current': values[1]})
        print('PASS: 96 lethal/nonlethal, mutual-hit, gore and facing cases match original retail execution', flush=True)

        # Each human update resets the flag; a different human cannot inherit it.
        save = prepare(entry=0x26118)
        ram = bytearray(save[OFFSET:OFFSET+SIZE])
        put(ram, HUMAN+0x12, 0); put(ram, HUMAN+0xe, 0)
        save[OFFSET:OFFSET+SIZE] = ram
        _, flag = run('next-human', save)
        assert flag == 0

        # Both rat reaction branches, clear/set latch, with living/dead contact.
        for hit, latch, hp in itertools.product((False, True), (0, 1), (0, 100)):
            name = f'reaction-hit{int(hit)}-l{latch}-hp{hp}'
            values = []
            for reference in (True, False):
                entry = (0x26c74 if hit else 0x26caa) if reference else (0x26b7e if hit else 0x26baa)
                save = prepare(reference, hp=hp, latch=latch, entry=entry)
                ram = bytearray(save[OFFSET:OFFSET+SIZE])
                put(ram, RAT+0x12, HUMAN if hit else 0)
                put(ram, 0x2e9ac if reference else 0x2ebd0, RAT)
                save[OFFSET:OFFSET+SIZE] = ram
                struct.pack_into('<I', save, 20+9*4, RAT)
                after, flag = run(('retail-' if reference else 'current-')+name, save,
                                  ['--noretailparity'] if reference else [])
                values.append(state(after, flag, reference))
                if not reference and not hit and hp == 100:
                    (out/f'latch-{"one" if latch else "zero"}.sav').write_bytes(save)
            assert values[0] == values[1], f'{name}: {values}'
            assert (values[1]['animation'] == 0xffffffff) == bool(latch)
            reports.append({'case': name, 'retail': values[0], 'current': values[1]})
        print('PASS: reset scope and both rat reaction paths match retail; unlatched damage remains', flush=True)

        # A faithful bare copy would install into the stale first owner match.
        # Keep the same native installer, but target the live display record.
        save = prepare(duplicate=True)
        ram, flag = run('duplicate-inactive-owner', save)
        assert state(ram, flag)['rat_script'] == 0x33826
        assert get(ram, 0x2eca0+0x32+2) == 0xabcdef, 'Inactive display was modified'
        for a in (0x2eca0+0x32, 0x2eca0+4*0x32):
            assert get(ram, a+0x18) == RAT
        print('PASS: recycled inactive owner cannot swallow the paired killing animation', flush=True)

        # Save halfway through the handler and each helper in the guest rts
        # chain. Resuming must produce the same complete guest RAM as uninterrupted.
        expected, _ = run('mid-handler-control', save)
        for pc in (0x26712, 0x28782, 0x21380, 0x213f4, 0x26734):
            snapshot = out/f'mid-{pc:x}.sav'
            run(f'capture-{pc:x}', save, ['--ko-snapshot', f'{pc:x}', str(snapshot)], args.probe)
            assert snapshot.exists(), f'{pc:x}: snapshot was not taken'
            saved = snapshot.read_bytes()
            assert saved[ko_offset(saved)] == 1
            resumed, flag = run(f'resume-{pc:x}', snapshot.read_bytes())
            assert resumed == expected and flag == 1, f'{pc:x}: mid-handler restore differs'
        print('PASS: five instruction-boundary saves retain the latch and complete the original helper chain', flush=True)

        one = (out/'latch-one.sav').read_bytes()
        (out/'legacy-v2.sav').write_bytes(versioned(one, 2))
        (out/'legacy-v3.sav').write_bytes(versioned(one, 3))
        for name, pos in (('invalid-ko', -TAIL-1), ('invalid-sword', -TAIL-2)):
            bad = bytearray(one); bad[pos] = 2; (out/(name+'.sav')).write_bytes(bad)
        (out/'truncated.sav').write_bytes(one[:-1]); (out/'trailing.sav').write_bytes(one+b'x')
        bad = bytearray(one); struct.pack_into('<l', bad, len(bad)-8, -1)
        (out/'invalid-streamer.sav').write_bytes(bad)
        run('warm-state', one, ['--ko-state-dir', str(out)], args.probe)
        print('PASS: actual warm loads, legacy loads, save roundtrip and failed-load atomicity', flush=True)
        (out/'results.json').write_text(json.dumps(reports, indent=2))
        print(f'Evidence: {out}')


if __name__ == '__main__':
    main()
