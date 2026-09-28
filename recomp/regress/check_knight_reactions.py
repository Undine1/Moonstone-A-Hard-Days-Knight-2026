#!/usr/bin/env python3
"""Compare AI-knight decisions/blocks with the original retail instructions.

Uses local original game images read only. Temporary actor records exercise the
actual table initializer, knight constructor, parry helper and AI routines.
All saves/logs remain in scratch. No copyrighted game data is included here.
"""
import argparse
import itertools
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OFFSET, SIZE = 104, 0x200000
ACTOR, TARGET, ITEMS, CODE = 0x100000, 0x100100, 0x100200, 0x104000

def get(ram, address, size=4):
    return int.from_bytes(ram[address:address+size], 'big')

def put(ram, address, value, size=4):
    ram[address:address+size] = (value & ((1 << (size*8))-1)).to_bytes(size, 'big')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    parser.add_argument('--probe', type=Path, default=ROOT/'recomp/build/knight_reaction_probe.exe')
    parser.add_argument('--before', type=Path)
    parser.add_argument('--fixture', type=Path, default=ROOT/'dist/MoonstoneNative/moonstone_combatrun.sav')
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    base = args.fixture.read_bytes()
    images = [base[OFFSET:OFFSET+SIZE], args.retail.read_bytes()[OFFSET:OFFSET+SIZE]]
    data = ROOT/'dist/MoonstoneNative/data'
    results = []
    with tempfile.TemporaryDirectory(prefix='moon-knight-') as temp:
        out = args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True, exist_ok=True)

        def run(save, ref=False, flags=(), exe=None, name='case'):
            path = out/(name+'.sav'); path.write_bytes(save)
            command = [str((exe or args.exe).resolve()), '--os', '--mod', str(data/'nb'),
                       '--dataset', str(data), '--diskdir', str(data), '--loadstate', str(path),
                       '--frames', '2', '--log', str(out/(name+'.log')), '--dumpram', str(out/(name+'.ram')),
                       '--savestate-at', '1', str(out/(name+'.end.sav'))]
            if ref: command += ['--noretailparity']
            command += list(flags)
            p = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (out/(name+'.stdout')).write_text(p.stdout+p.stderr)
            assert p.returncode == 0, (command, p.stdout[-2000:], p.stderr[-2000:])
            ram = (out/(name+'.ram')).read_bytes()
            regs = struct.unpack_from('<21I', (out/(name+'.end.sav')).read_bytes(), 20)
            assert regs[16] == (0x27f26 if ref else 0x27ec8), (name, hex(regs[16]))
            return ram, regs

        def prepare(ref=False, mode='reaction', flag=0, attack=8, action=0x1c,
                    hp=100, facing=3, distance=50, difficulty=0, seed=0x12345678):
            ram = bytearray(images[ref]); ram[ACTOR:ACTOR+0x1000] = bytes(0x1000)
            regs = [0]*21; regs[15] = regs[19] = 0x1ff000
            regs[16] = CODE; regs[17] = 0x2700; regs[1] = distance
            current = 0x2e9ac if ref else 0x2ebd0
            target = 0x2e9b0 if ref else 0x2ebd4
            next_anim = 0x2e8d2 if ref else 0x2eaf8
            idle = 0x310bc if ref else 0x31308
            put(ram, current, ACTOR); put(ram, target, TARGET); put(ram, next_anim, idle)
            put(ram, ACTOR+0x4d, 0x10, 1); put(ram, TARGET+0x4d, 0x0c, 1)
            put(ram, ACTOR+0x50, hp, 2); put(ram, TARGET+0x50, 100, 2)
            put(ram, ACTOR+0x40, action, 2); put(ram, TARGET+0x40, attack, 2)
            put(ram, ACTOR+0x68, (flag << 8) | 0x5a, 2)
            put(ram, ACTOR+0xa, 1, 1); put(ram, TARGET+0xa, facing, 1)
            put(ram, ACTOR+4, 100, 2); put(ram, TARGET+4, 100+distance, 2)
            put(ram, ACTOR+8, 100, 2); put(ram, TARGET+8, 100, 2)
            put(ram, TARGET+0x58, 0x16); put(ram, TARGET+0x46, 2, 1)
            put(ram, TARGET+0x60, ITEMS)
            put(ram, 0x38f68 if ref else 0x391a8, seed)
            put(ram, 0x30150 if ref else 0x30394, difficulty, 2)
            put(ram, 0x427e0 if ref else 0x42b74, action, 2)
            entry = (0x42260 if ref else 0x425e2) if mode == 'reaction' else (0x423f0 if ref else 0x4278c)
            if mode == 'block': put(ram, ACTOR+0x12, TARGET)
            # Real initializers populate the actual animation, damage and guard
            # tables; the retail menu snapshot has not initialized them yet.
            code = bytearray()
            def emit(op, value): code.extend(op.to_bytes(2,'big')+value.to_bytes(4,'big'))
            emit(0x4eb9, 0x244fc if ref else 0x24420)
            for actor in [ACTOR, TARGET]:
                emit(0x43f9, actor); emit(0x4eb9, 0x24e1a if ref else 0x24d3e)
            emit(0x41f9, ACTOR); emit(0x4ef9, entry)
            ram[CODE:CODE+len(code)] = code
            put(ram, 0x27f26 if ref else 0x27ec8, 0x60fe, 2)
            save = bytearray(base); save[OFFSET:OFFSET+SIZE] = ram
            struct.pack_into('<21I', save, 20, *regs)
            return save

        def result(ram, regs, ref):
            return dict(action=get(ram, ACTOR+0x40, 2), flags=get(ram, ACTOR+0x68, 2),
                        hp=get(ram, ACTOR+0x50, 2), block=get(ram, 0x26598 if ref else 0x2647a, 2),
                        animation=get(ram, 0x2e8d2 if ref else 0x2eaf8)-(0x310bc if ref else 0x31308),
                        rng=get(ram, 0x38f68 if ref else 0x391a8), ccr=regs[17]&31)

        def compare(case):
            values=[]
            for ref in [False, True]:
                ram, regs = run(prepare(ref, **case), ref)
                values.append(result(ram, regs, ref))
            assert values[0] == values[1], (case, values)
            assert values[0]['flags'] & 0x7fff == ((case['flag'] << 8) | 0x5a) & 0x7fff
            results.append(dict(case=case, result=values[0]))

        # Both defence choices, non-reactive attacks, every difficulty, both
        # latch states. Original random code and threshold tables execute.
        for flag, attack, difficulty in itertools.product([0,0x80], [0,4,8,0x20], range(8)):
            compare(dict(mode='reaction', flag=flag, attack=attack, difficulty=difficulty))
        for flag, facing, distance, seed in itertools.product([0x25,0xa5], [1,3], [120,121], [1,0xffffffff]):
            compare(dict(mode='reaction', flag=flag, attack=8, facing=facing, distance=distance, seed=seed))
        print('PASS: 96 original-code reaction comparisons, including distance/facing/RNG boundaries', flush=True)

        # Native shared helper: valid/mismatched blocks, already-latched state,
        # dead targets and a normal sword parry that does not set the latch.
        for flag, actions, hp, facing in itertools.product([0,0x80,0x25,0xa5],
                   [(0x1c,4),(0x1c,0x20),(0x1c,0x14),(0x10,8),(0x1c,8),(0,8)], [0,100], [1,3]):
            action, attack = actions
            compare(dict(mode='block', flag=flag, action=action, attack=attack, hp=hp, facing=facing))
        print('PASS: 192 original-code block/damage/death comparisons; unrelated flag bits preserved', flush=True)

        for mode, attack in [('reaction',8), ('block',4)]:
            save=prepare(mode=mode, attack=attack, flag=0x80 if mode=='reaction' else 0)
            ram,regs=run(save, name=mode+'-fixed')
            fixed=result(ram,regs,False)
            ram,regs=run(save, flags=['--noknightreactionfix'], name=mode+'-off')
            old=result(ram,regs,False)
            assert fixed != old, (mode,fixed,old)
            if args.before:
                prior,_=run(save,exe=args.before,name=mode+'-before')
                assert ram==prior,'Isolated rollback differs from the frozen previous runtime'
            results.append(dict(mode=mode,fixed=fixed,disabled=old))
        print('PASS: both omissions reproduced independently; isolated rollback matches the previous build',flush=True)
        # Retain initialized boundary fixtures for C-probe native flags, guard
        # tests and actual warm loads, instead of testing a host arithmetic copy.
        for mode, attack in [('reaction',8), ('block',4)]:
            save=prepare(mode=mode, attack=attack, flag=0x80 if mode=='reaction' else 0)
            pc=0x4261c if mode=='reaction' else 0x427c4
            run(save,flags=['--knight-snapshot',f'{pc:x}',str(out/(mode+'-entry.sav'))],exe=args.probe)
        run(prepare(),flags=['--knight-check-dir',str(out),'--knight-retail',str(args.retail.resolve())],exe=args.probe,name='warm-checks')
        (out/'results.json').write_text(json.dumps({'comparisons':288,'executions':576,'results':results},indent=2))

if __name__=='__main__': main()
