#!/usr/bin/env python3
"""Additional Stonehenge warm-load, legacy transaction and quest checks.

Run check_danu.py first. This consumes its scratch checkpoints read-only and
writes separate output. It never changes an original fixture or installation.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess

from check_danu import ROOT, RAM_OFFSET, RAM_SIZE, get, put, saved_ram, persistent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT / 'recomp/build/danu_probe.exe')
    ap.add_argument('--fixtures', type=Path, default=ROOT / 'recomp/build/item-action-20260923')
    ap.add_argument('--checks', type=Path, required=True)
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    checks, fixtures, out = args.checks.resolve(), args.fixtures.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    results = []

    def run(name, save, frames=1250, script='0:.', exe=None, extra=()):
        path = out / (name + '.sav')
        path.write_bytes(save)
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data / 'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                   '--frames', str(frames), '--script', script, '--log', str(out / (name + '.log')),
                   '--dumpram', str(out / (name + '.ram')), *extra]
        process = subprocess.run(command, capture_output=True, text=True, timeout=60,
                                 creationflags=0x08000000)
        (out / (name + '.stdout')).write_text(process.stdout + process.stderr)
        assert process.returncode == 0 and 'unmapped=0' in process.stdout, (name, process)
        ram = (out / (name + '.ram')).read_bytes()
        results.append({'name': name, 'command': command, 'state': persistent(ram)})
        return ram

    process = subprocess.run([str(args.probe.resolve()), '--danu-guards', str(fixtures),
                              '--log', str(out / 'guards.log')], capture_output=True, text=True,
                             timeout=30, creationflags=0x08000000)
    (out / 'guards.stdout').write_text(process.stdout + process.stderr)
    assert process.returncode == 0, process
    print(process.stdout.strip(), flush=True)

    warm_count = 0
    for name in ('potion', 'ring'):
        lines = (checks / ('capture-' + name + '.log')).read_text(errors='replace')
        captures = dict((int(pc, 16), int(index)) for index, pc in
                        re.findall(r'DANU-BOUNDARY index=(\d+) pc=([0-9a-f]+)', lines))
        seed = (checks / ('capture-' + name + '.sav')).read_bytes()
        expected = (checks / ('capture-' + name + '.ram')).read_bytes()
        for pc in (0x2cda6, 0x2cd6e, 0x2cd78, 0x2cfee, 0x2cffc, 0x2d002,
                   0x2d008, 0x2d010, 0x227de, 0x227ee, 0x22800, 0x22806):
            assert pc in captures, (name, hex(pc))
            checkpoint = checks / ('capture-' + name) / f'mid-{captures[pc]:02}.sav'
            ram = run(f'warm-{name}-{pc:x}', seed, 2600, '10:f,18:.', args.probe,
                      ['--danu-warm-load', str(checkpoint)])
            assert persistent(ram) == persistent(expected), (name, hex(pc))
            warm_count += 1
    print(f'PASS: {warm_count} warm loads after a completed offering preserve exactly one item/life change', flush=True)

    legacy_count = 0
    for name in ('armour', 'special-sword'):
        capture = out / ('legacy-' + name)
        capture.mkdir(exist_ok=True)
        save = (fixtures / ('checked-' + name + '.sav')).read_bytes()
        run('legacy-' + name, save, 1250, '10:f,18:.', args.probe,
            ['--nodanufix', '--danu-capture', str(capture)])
        lines = (out / ('legacy-' + name + '.log')).read_text(errors='replace')
        captures = dict((int(pc, 16), int(index)) for index, pc in
                        re.findall(r'DANU-BOUNDARY index=(\d+) pc=([0-9a-f]+)', lines))
        for pc in (0x2cfde, 0x2cd6e, 0x2cd78, 0x2cfee):
            snapshot = (capture / f'mid-{captures[pc]:02}.sav').read_bytes()
            ram = run(f'legacy-reject-{name}-{pc:x}', snapshot, 100)
            before = saved_ram(snapshot)
            assert persistent(ram)['items'] == persistent(before)['items'], (name, hex(pc))
            actor = get(before, 0x2fb08)
            assert ram[actor + 0x46:actor + 0x84] == before[actor + 0x46:actor + 0x84], (name, hex(pc))
            assert get(ram, 0x2fb1c) == 3 and get(ram, 0x2cfda, 2) == 65535, (name, hex(pc))
            legacy_count += 1
        # Already committed old corruption is left intact; the caller rejects
        # the unsupported selection rather than granting a further life.
        for pc in (0x2d002, 0x227de):
            snapshot = (capture / f'mid-{captures[pc]:02}.sav').read_bytes()
            ram = run(f'legacy-no-reward-{name}-{pc:x}', snapshot)
            before = saved_ram(snapshot)
            assert persistent(ram)['items'] == persistent(before)['items'], (name, hex(pc))
            actor = get(before, 0x2fb08)
            assert get(ram, actor + 0x49, 1) == get(before, actor + 0x49, 1), (name, hex(pc))
            assert get(ram, 0x2cfda, 2) == 65535, (name, hex(pc))
            legacy_count += 1
    print(f'PASS: {legacy_count} legacy transaction boundaries reject invalid offers; completed old writes are not guessed away', flush=True)

    # The separate matching-Moonstone delivery branch must remain identical.
    quest_count = 0
    for bit, home_scene in ((1, 0x31), (2, 0x2d), (4, 0x2e), (8, 0x2e)):
        save = bytearray((fixtures / 'checked-entry.sav').read_bytes())
        ram = saved_ram(save)
        actor = get(ram, 0x2e0bc)
        put(ram, get(ram, actor + 0x60) + 0x16, bit, 1)
        put(ram, 0x2e0ce, home_scene, 2)
        struct.pack_into('<I', save, 20 + 16 * 4, 0x22768)
        save[RAM_OFFSET:RAM_OFFSET + RAM_SIZE] = ram
        old = run(f'quest-before-{bit}', save, 1250, '60:f,68:.', args.before_exe,
                  ['--wav', str(out / f'quest-before-{bit}.wav')])
        new = run(f'quest-after-{bit}', save, 1250, '60:f,68:.', extra=
                  ['--wav', str(out / f'quest-after-{bit}.wav')])
        assert new == old, bit
        assert (out / f'quest-before-{bit}.wav').read_bytes() == (out / f'quest-after-{bit}.wav').read_bytes(), bit
        assert b'DELIVERY arm @22820' in (out / f'quest-after-{bit}.log').read_bytes(), bit
        quest_count += 1
    print(f'PASS: all {quest_count} matching Moonstone deliveries retain identical full RAM and PCM', flush=True)
    (out / 'verification.json').write_text(json.dumps({'warm_boundaries': warm_count,
        'legacy_boundaries': legacy_count, 'quest_deliveries': quest_count, 'runs': results}, indent=2))


if __name__ == '__main__':
    main()
