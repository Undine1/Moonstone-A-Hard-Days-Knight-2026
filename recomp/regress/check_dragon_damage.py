#!/usr/bin/env python3
"""Compare dragon damage against original retail instructions, including saves.

Uses local game/reference material read only. All generated saves/logs stay in
--output or a disposable directory. Build dragon_damage_probe.c separately for
actual warm loads and instruction-boundary snapshots; never ship that probe.
"""
import argparse
import itertools
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OFFSET,SIZE=104,0x200000
ATTACKER,VICTIM,ITEMS=0x100000,0x100100,0x100200

def get(r,a,n=4):
    return int.from_bytes(r[a:a+n],'big')

def put(r,a,v,n=4):
    r[a:a+n]=(v&((1<<(n*8))-1)).to_bytes(n,'big')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',type=Path,default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe',type=Path,default=ROOT/'recomp/build/dragon_damage_probe.exe')
    ap.add_argument('--fixture',type=Path,default=ROOT/'dist/MoonstoneNative/moonstone_combatrun.sav')
    ap.add_argument('--retail',type=Path,required=True)
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    base=args.fixture.read_bytes();current=base[OFFSET:OFFSET+SIZE]
    retail=args.retail.read_bytes()[OFFSET:OFFSET+SIZE]
    data=ROOT/'dist/MoonstoneNative/data'
    results=[]
    with tempfile.TemporaryDirectory(prefix='moon-dragon-') as temp:
        out=args.output.resolve() if args.output else Path(temp)
        out.mkdir(parents=True,exist_ok=True)

        def run(name,source,extra=(),exe=None):
            path=out/(name+'.sav');path.write_bytes(source)
            command=[str((exe or args.exe).resolve()),'--os','--mod',str(data/'nb'),
                     '--dataset',str(data),'--diskdir',str(data),'--loadstate',str(path),
                     '--frames','2','--log',str(out/(name+'.log')),'--dumpram',str(out/(name+'.ram')),
                     '--savestate-at','1',str(out/(name+'.end.sav')),*extra]
            p=subprocess.run(command,capture_output=True,text=True,timeout=30)
            (out/(name+'.stdout')).write_text(p.stdout+p.stderr)
            assert p.returncode==0,(name,p.stdout,p.stderr)
            final=(out/(name+'.end.sav')).read_bytes()
            return (out/(name+'.ram')).read_bytes(),struct.unpack_from('<21I',final,20)

        def prepare(reference=False,talismans=0,armour=0x1b,kind=0x2c,action=0x14,hp=100,entry=False):
            r=bytearray(retail if reference else current)
            r[ATTACKER:ATTACKER+0x1000]=bytes(0x1000)
            regs=[0]*21;regs[9]=VICTIM;regs[8]=ATTACKER
            regs[15]=regs[19]=0x1ff000;regs[16]=0x104000;regs[17]=0x2700
            put(r,0x104000,0x4eb9,2);put(r,0x104002,0x268f0 if reference else 0x267ba)
            put(r,0x104006,0x4ef9,2);put(r,0x104008,0x2659a if reference else 0x2647c)
            put(r,0x1ff000,0x1ef000);put(r,0x1ef000,0x60fe,2)
            put(r,ATTACKER+0x4d,kind,1);put(r,ATTACKER+0x40,action,2)
            put(r,VICTIM+0x12,ATTACKER);put(r,VICTIM+0x60,ITEMS)
            put(r,VICTIM+0x50,hp,2);put(r,VICTIM+0x5c,armour);put(r,ITEMS+8,talismans,1)
            stop=(0x267aa if reference else 0x2668c) if kind==0x2c else (0x2679c if reference else 0x2667e)
            put(r,stop,0x60fe,2) # stop after actual HP subtraction, before presentation
            if entry:regs[16]=0x267a0 if reference else 0x26682;regs[0]=44
            save=bytearray(base);save[OFFSET:OFFSET+SIZE]=r
            struct.pack_into('<21I',save,20,*regs)
            return save,stop

        for talismans,armour,attack in itertools.product(range(4),range(0x1b,0x1f),
                     [(0x2c,0x14),(0x14,4),(0x14,8),(0x28,0x20)]):
            values=[]
            for ref in [False,True]:
                save,stop=prepare(ref,talismans,armour,*attack)
                name=f'matrix-{int(ref)}-t{talismans}-a{armour:x}-k{attack[0]:x}-m{attack[1]:x}'
                ram,regs=run(name,save,['--noretailparity','--noretailsfx'] if ref else [])
                assert regs[16]==stop,name
                values.append(get(ram,VICTIM+0x50,2))
            assert values[0]==values[1],(name,values)
            results.append(dict(talismans=talismans,armour=armour,attack=attack,damage=100-values[0]))
        print('PASS: 128 original-dispatch executions; all dragon paths, four talisman counts and four armours match retail',flush=True)

        for hp,talismans in itertools.product([-1,0,1,4,5,9,10,11,34,53],range(4)):
            values=[]
            for ref in [False,True]:
                save,stop=prepare(ref,talismans,hp=hp)
                ram,regs=run(f'health-{int(ref)}-{hp}-t{talismans}',save,
                              ['--noretailparity','--noretailsfx'] if ref else [])
                assert regs[16]==stop
                values.append(get(ram,VICTIM+0x50,2))
            assert values[0]==values[1],(hp,talismans,values)
        print('PASS: 80 original-dispatch executions at lethal/nonlethal HP boundaries match retail',flush=True)

        save,stop=prepare(entry=True)
        (out/'entry-old.sav').write_bytes(save)
        newer=bytearray(save);put(newer,OFFSET+0x26682,0x303c,2)
        (out/'entry-new.sav').write_bytes(newer)
        retail_entry,_=prepare(True,entry=True)
        (out/'entry-retail.sav').write_bytes(retail_entry)
        for talismans in range(4):
            for opcode,flags in itertools.product([0x0440,0x303c],[[],['--nodragondamagefix'],['--noretailparity']]):
                saved,stop=prepare(talismans=talismans,entry=True)
                put(saved,OFFSET+0x26682,opcode,2)
                ram,regs=run(f'entry-t{talismans}-o{opcode:x}-f{len(flags)}-'+''.join(flags),saved,flags)
                base_damage=34 if flags else 10
                damage=base_damage>>talismans
                if flags!=['--noretailparity']:damage=max(5,damage)
                assert regs[16]==stop and get(ram,VICTIM+0x50,2)==100-damage
        print('PASS: old/patched entry saves, isolated rollback and full parity-off (including its original floor behavior)',flush=True)

        expected,_=run('resume-control',save)
        for pc in [0x26682,0x26686,0x266b4,0x266be,0x266c0,0x26688,0x2668c]:
            snapshot=out/f'mid-{pc:x}.sav'
            run(f'capture-{pc:x}',save,['--dragon-snapshot',f'{pc:x}',str(snapshot)],args.probe)
            assert snapshot.exists(),hex(pc)
            after,_=run(f'resume-{pc:x}',snapshot.read_bytes())
            assert after==expected,f'{pc:x}: resumed RAM differs'
        print('PASS: seven actual instruction-boundary saves resume to identical full guest RAM',flush=True)
        run('warm-loads',save,['--dragon-warm-dir',str(out)],args.probe)
        print('PASS: warm save/load, native flags/cycles and opcode/lineage guards (see warm-loads.stdout)',flush=True)
        (out/'results.json').write_text(json.dumps({'matrix':results,'retail_executions':208,
            'cold_entry_ab_cases':24,'mid_instruction_saves':7,'warm_flags_guards':'PASS'},indent=2))

if __name__=='__main__':main()
