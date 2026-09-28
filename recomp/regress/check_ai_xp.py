#!/usr/bin/env python3
"""Compare AI XP with retail, cold-load award boundaries and audit a map replay.

Build ai_xp_probe.c separately. Original retail/current saves are read-only;
every executable invocation specifies a scratch --log. Never deploy the probe.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OFFSET, SIZE, STOP, STACK = 104, 0x200000, 0x1ef000, 0x1ff000


def put(ram, a, v, n=4):
    ram[a:a+n] = (v & ((1 << (8*n))-1)).to_bytes(n, 'big')


def machine(save, pc=STOP):
    save = bytearray(save)
    ram = bytearray(save[OFFSET:OFFSET+SIZE])
    put(ram, STOP, 0x60fe, 2)
    put(ram, STACK, STOP)
    regs = [0]*21
    regs[15] = regs[19] = STACK
    regs[16], regs[17] = pc, 0x2700
    struct.pack_into('<21I', save, 20, *regs)
    save[OFFSET:OFFSET+SIZE] = ram
    return save


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/ai_xp_probe.exe')
    ap.add_argument('--retail', type=Path, required=True)
    ap.add_argument('--before', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT/'dist/MoonstoneNative/data'

    def run(name, save, *, frames=2, flags=(), exe=None, snapshot=True):
        if isinstance(save, (bytes, bytearray)):
            path = out/(name+'.sav')
            path.write_bytes(save)
        else:
            path = save.resolve()
        command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data/'nb'),
                   '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                   '--frames', str(frames), '--log', str(out/(name+'.log')),
                   '--dumpram', str(out/(name+'.ram')), *flags]
        if snapshot:
            command += ['--savestate-at', str(frames-1), str(out/(name+'.end.sav'))]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (out/(name+'.stdout')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, (name, result.stdout[-3000:], result.stderr[-3000:])
        return (out/(name+'.ram')).read_bytes(), result.stdout

    source = (ROOT/'dist/MoonstoneNative/moonstone_combatrun.sav').read_bytes()
    run('normalize', machine(source))
    current = (out/'normalize.end.sav').read_bytes()
    assert struct.unpack_from('<I', current, 8)[0] == 5
    (out/'current.sav').write_bytes(current)
    _, stdout = run('oracle', current, exe=args.probe,
                    flags=['--xp-tests', str(out), str(args.retail.resolve())])
    print(stdout, flush=True)
    for pc in (0x21708, 0x2170c, 0x21712, 0x21716, 0x21bf2, 0x21bf8, 0x21bfc):
        ram, _ = run(f'cold-{pc:x}', out/f'boundary-{pc:x}.sav')
        assert int.from_bytes(ram[0x2e82a:0x2e82c], 'big') == 3
        after = (out/f'cold-{pc:x}.end.sav').read_bytes()
        assert struct.unpack_from('<I', after, 84)[0] == STOP
        assert len(after) == len(current), 'Save format changed'
    print('PASS: seven production cold loads award exactly once; save v5 unchanged', flush=True)

    if args.before:
        fixture = ROOT/'dist/MoonstoneNative/moonstone_townarmor.sav'
        flags = ['--script', '10:u,36:.,40:r,46:.,60:f,66:.,800:d,844:.,850:l,853:.,860:f,866:.',
                 '--poke8', '700:2e83b:1e', '--maplog']
        result = {}
        for name, exe, extra in [('before', args.before, []), ('fixed', args.exe, []),
                                 ('disabled', args.exe, ['--noaixpfix'])]:
            ram, text = run('townarmor-'+name, fixture, frames=2600, flags=flags+extra,
                            exe=exe, snapshot=False)
            result[name] = {'ram': ram, 'fnv': re.search(r'ram_fnv=([0-9a-f]+)', text).group(1)}
        assert result['before']['ram'] == result['disabled']['ram']
        changed = [i for i, (a, b) in enumerate(zip(result['before']['ram'], result['fixed']['ram'])) if a != b]
        report = {'fnv': {k: v['fnv'] for k, v in result.items()},
                  'changed_bytes': [{'address': f'{a:06x}', 'before': result['before']['ram'][a],
                                     'fixed': result['fixed']['ram'][a]} for a in changed],
                  'fixed_events': [line for line in (out/'townarmor-fixed.log').read_text().splitlines()
                                   if line.startswith('AI-XP')]}
        (out/'townarmor-diff.json').write_text(json.dumps(report, indent=2))
        print('PASS: disabled mode reproduces previous RAM exactly; fixed map replay changes:', report, flush=True)


if __name__ == '__main__':
    main()
