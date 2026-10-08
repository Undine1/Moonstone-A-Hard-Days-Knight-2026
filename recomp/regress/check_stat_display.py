#!/usr/bin/env python3
"""Exercise native stat formatting, real inventory panels and old save resumes.

All fixture paths are read-only. Every engine log/output goes to --output.
"""
from pathlib import Path
import argparse, hashlib, json, re, struct, subprocess

ROOT = Path(__file__).resolve().parents[2]
OFFSET, SIZE = 104, 0x200000

def get(b, a, n=4):
    return int.from_bytes(b[a:a+n], 'big')

def put(b, a, value, n=4):
    b[a:a+n] = value.to_bytes(n, 'big')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--images', type=Path, required=True, help='Original port.ram and retail.ram')
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/stat_display_probe.exe')
    ap.add_argument('--entry', type=Path, default=ROOT/'recomp/build/item-action-20260923/checked-entry.sav')
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    data = ROOT/'dist/MoonstoneNative/data'; reports = []

    def execute(command, name):
        p = subprocess.run(command, capture_output=True, text=True, timeout=60, creationflags=0x08000000)
        (out/(name+'.stdout')).write_text(p.stdout+p.stderr)
        assert p.returncode == 0, (name, p.stdout, p.stderr)
        return p

    p = execute([str(args.probe.resolve()), '--stat-display-oracle', str(args.images.resolve()),
                 '--log', str(out/'native.log')], 'native')
    assert '1440' in p.stdout and '18 scope' in p.stdout
    print(p.stdout.strip(), flush=True)

    def run(name, save, frames=400, extra=(), exe=None, script='0:.'):
        command = [str((exe or args.probe).resolve()), '--os', '--mod', str(data/'nb'), '--dataset', str(data),
                   '--diskdir', str(data), '--loadstate', str(save), '--frames', str(frames),
                   '--script', script, '--log', str(out/(name+'.log')), '--dumpram', str(out/(name+'.ram')), *extra]
        p = execute(command, name)
        log = (out/(name+'.log')).read_text()
        if '--loadstate-at' in extra:
            assert '--- loadstate-at' in log and 'UNMAPPED' not in log.split('--- loadstate-at',1)[1], name
        else:
            assert 'unmapped=0' in p.stdout, (name, p.stdout)
        r = (out/(name+'.ram')).read_bytes(); assert len(r)==SIZE
        rows = [(int(a,16), int(f,16), int(actual), int(text)) for a,f,actual,text in
                re.findall(r'STAT-DISPLAY actor=([0-9a-f]+) field=([0-9a-f]+) actual=(\d+) text=(\d+)', log)]
        reports.append(dict(name=name, command=command, rows=rows, ram_sha256=hashlib.sha256(r).hexdigest()))
        return r, rows

    def persistent(r):
        actors = [0x2e7dc+i*0x84 for i in range(4)]
        return [r[a:a+0x84] for a in actors] + [r[get(r,a+0x60):get(r,a+0x60)+24] for a in actors]

    captured = out/'old-before-format.sav'
    on_menu = None
    for scene in (0,1,3,10):
        for owner in range(4):
            name = f's{scene}-p{owner+1}'
            b = bytearray(args.entry.read_bytes()); r = bytearray(b[OFFSET:OFFSET+SIZE])
            actor = 0x2e7dc+owner*0x84; other = 0x2e7dc+((owner+1)%4)*0x84
            put(r,0x2e0bc,actor);put(r,0x2e0c0,other)
            for a in (actor,other):
                r[a+0x46:a+0x49] = bytes((2,1,8) if a==actor else (4,5,6))
                put(r,a+0x4e,10,2);put(r,a+0x50,12,2)
            b[OFFSET:OFFSET+SIZE]=r; path=out/(name+'-entry.sav');path.write_bytes(b)
            states = []
            for disabled in (False,True):
                suffix = '-off' if disabled else '-on'
                save = out/(name+suffix+'.sav')
                extra = ['--stat-display-scene',str(scene),'--poke','0x2a3c8:0x100:w@0x2c214',
                         '--savestate-at','399',str(save)]
                if disabled: extra += ['--nostatdisplayfix']
                if scene==0 and owner==0 and disabled: extra += ['--stat-display-capture',str(captured)]
                state, rows = run(name+suffix,path,extra=extra,script='60:f,68:.')
                assert rows and {f for _,f,_,_ in rows}=={0x46,0x47,0x48}, name
                if disabled:
                    assert any(actual!=shown for _,_,actual,shown in rows), name
                else:
                    assert all(actual==shown for _,_,actual,shown in rows), (name,rows)
                if scene==1:
                    assert {a for a,_,_,_ in rows}=={actor,other}, (name, rows)
                states.append(state)
                if scene==0 and owner==0 and not disabled: on_menu=save
            assert persistent(states[0])==persistent(states[1]), name
    print('PASS: 32 original inventory/knight-loot/Stonehenge/dragon panels; all four owners, both knight columns, preserved gameplay',flush=True)

    # Old bad argument survives in the save immediately before the native call.
    b = captured.read_bytes()
    assert struct.unpack_from('<I',b,20)[0]>255 and struct.unpack_from('<I',b,20+16*4)[0]==0x2c24e
    for warm in (False,True):
        extra = ['--memlog'] + (['--loadstate-at','120',str(captured)] if warm else [])
        name = 'resume-'+('warm' if warm else 'cold')
        state,rows=run(name,captured,360,extra)
        assert rows and all(a==s for _,_,a,s in rows), (name,rows)
        actual,_=run(name+'-production',captured,360,extra,args.exe)
        assert actual==state, name+': observer changed RAM'
        old,_=run(name+'-before',captured,360,extra,args.before_exe)
        disabled,_=run(name+'-disabled',captured,360,[*extra,'--nostatdisplayfix'],args.exe)
        assert old==disabled and persistent(actual)==persistent(old), name
    print('PASS: eight cold/warm old-save replays; production matches observer, disabling fix restores complete prior RAM',flush=True)

    # Exercise a real stat transaction/redraw, so formatting must not alter its
    # eligibility, XP payment, CON or HP arithmetic.
    b=bytearray(on_menu.read_bytes());r=b[OFFSET:OFFSET+SIZE];actor=get(r,0x2fb08)
    for i in range(100):
        h=get(r,0x3a96c)+24*i
        if get(r,h+20,2)==3 and get(r,h+22,2)==0x47:
            struct.pack_into('>HH',b,OFFSET+0x392d4,get(r,h+12,2)+2,get(r,h+14,2)+2);break
    else: raise AssertionError('CON upgrade missing')
    path=out/'upgrade-entry.sav';path.write_bytes(b);states=[]
    for disabled in (False,True):
        name='upgrade-'+('off' if disabled else 'on')
        state,rows=run(name,path,180,['--nostatdisplayfix'] if disabled else [],script='10:f,18:.')
        assert state[actor+0x47]==2 and get(state,actor+0x4e,2)==10-get(r,0x30528,2)
        assert get(state,actor+0x50,2)==get(r,actor+0x50,2)+10
        assert get(state,actor+0x54,2)==get(r,actor+0x54,2)+10
        if not disabled: assert rows and all(a==s for _,_,a,s in rows)
        states.append(state)
    assert persistent(states[0])==persistent(states[1])
    print('PASS: native CON purchase/redraw retains XP cost and both HP changes',flush=True)
    (out/'verification.json').write_text(json.dumps(dict(native_cases=1440,guards=18,panel_runs=32,
        save_runs=8,upgrade_runs=2,runs=reports),indent=2))

if __name__=='__main__': main()
