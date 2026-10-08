#!/usr/bin/env python3
"""Native retail oracle and full-game stat-menu/save checks; no installed writes."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess

ROOT=Path(__file__).resolve().parents[2]
OFFSET=104;SIZE=0x200000
def get(b,a,n=4):return int.from_bytes(b[a:a+n],'big')
def put(b,a,v,n=4):b[a:a+n]=v.to_bytes(n,'big')
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',type=Path,default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe',type=Path,default=ROOT/'recomp/build/stat_cap_probe.exe')
    ap.add_argument('--fixtures',type=Path,default=ROOT/'recomp/build/item-action-20260923')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    data=ROOT/'dist/MoonstoneNative/data';reports=[]
    def execute(command,name):
        p=subprocess.run(command,capture_output=True,text=True,timeout=60,creationflags=0x08000000)
        (out/(name+'.stdout')).write_text(p.stdout+p.stderr)
        assert p.returncode==0,(name,p.stdout,p.stderr)
        return p
    p=execute([str(args.probe.resolve()),'--stat-oracle',str(args.fixtures.resolve()),'--log',str(out/'native.log')],'native')
    assert 'current=0 cached=0' in p.stdout
    print(p.stdout.strip(),flush=True)
    def run(name,save,frames,script='0:.',extra=(),probe=False):
        path=out/(name+'.sav');path.write_bytes(save)
        cmd=[str((args.probe if probe else args.exe).resolve()),'--os','--mod',str(data/'nb'),'--dataset',str(data),'--diskdir',str(data),'--loadstate',str(path),'--frames',str(frames),'--script',script,'--log',str(out/(name+'.log')),'--dumpram',str(out/(name+'.ram')),*extra]
        p=execute(cmd,name);assert 'unmapped=0' in p.stdout,name
        ram=(out/(name+'.ram')).read_bytes();assert len(ram)==SIZE
        reports.append(dict(case=name,ram_sha256=hashlib.sha256(ram).hexdigest()))
        return ram
    def entry(name,scene,player,con,end,xp,cached=False):
        save=bytearray((args.fixtures/'checked-entry.sav').read_bytes());ram=bytearray(save[OFFSET:OFFSET+SIZE])
        actor=0x2e7dc+player*0x84;price=[3,2,1,1][player]
        put(ram,0x2e0bc,actor);put(ram,actor+0x46,2,1);put(ram,actor+0x47,con,1);put(ram,actor+0x48,end,1)
        put(ram,actor+0x4e,price if xp else 0,2);put(ram,actor+0x50,13,2)
        put(ram,0x30528,price,2);save[OFFSET:OFFSET+SIZE]=ram
        extra=['--stat-scene',str(scene),'--savestate-at','399',str(out/(name+'-wait.sav'))]
        if cached:extra+=['--nostatcapfix']
        run(name+'-entry',save,400,'60:f,68:.',extra,True)
        result=(out/(name+'-wait.sav')).read_bytes();r=result[OFFSET:OFFSET+SIZE]
        assert get(r,0x2fb1c)==scene and get(r,0x2fb08)==actor
        return result
    def target(save,field):
        r=save[OFFSET:OFFSET+SIZE];base=get(r,0x3a96c)
        for n in range(100):
            h=base+n*24
            if not get(r,h+4,2):break
            if get(r,h+22,2)==field and get(r,h+8) in (0x2fe52,0x2fe60):
                return h,get(r,h+12,2)+2,get(r,h+14,2)+2
        raise AssertionError(('missing stat',hex(field)))
    def position(save,field):
        s=bytearray(save);h,x,y=target(s,field);struct.pack_into('>HH',s,OFFSET+0x392d4,x,y)
        return s,h
    def outcome(before,after,field,gain):
        a=get(before,0x2fb08);price=get(before,0x30528,2)
        for f in (0x46,0x47,0x48):assert after[a+f]==before[a+f]+(gain if f==field else 0),(hex(f),gain)
        assert get(after,a+0x4e,2)==get(before,a+0x4e,2)-gain*price
        assert get(after,a+0x50,2)==get(before,a+0x50,2)+(10*gain if field==0x47 else 0)
        assert get(after,a+0x54,2)==get(before,a+0x54,2)+(10*gain if field==0x47 else 0)
        assert after[a+0x56]==before[a+0x56]+(2*gain if field==0x48 else 0)
        for n in range(4):
            other=0x2e7dc+n*0x84
            if other!=a:assert after[other:other+0x84]==before[other:other+0x84]
        inv=get(before,a+0x60);assert after[inv:inv+24]==before[inv:inv+24]
    cases=[('con-capped',5,1,0x47,1,0),('con-allowed',4,5,0x47,1,1),
           ('end-capped',1,5,0x48,1,0),('end-allowed',5,4,0x48,1,1),
           ('con-existing8',8,1,0x47,1,0),('no-xp',1,1,0x47,0,0)]
    seeds={};ui=0
    for scene in (0,3,10):
        for player in range(4):
            for title,con,end,field,xp,gain in cases:
                cached=bool(player%2);name=f's{scene}-p{player+1}-{title}'
                save=entry(name,scene,player,con,end,xp,cached)
                seeded,h=position(save,field);before=seeded[OFFSET:OFFSET+SIZE]
                after=run(name+'-click',seeded,100,'10:f,18:.')
                outcome(before,after,field,gain)
                if scene!=3:
                    label=get(after,get(after,h+8))
                    assert (b'Constitution' if field==0x47 else b'Endurance') in after[label:label+32]
                if not gain:assert get(after,h+16)==(field-0x46)*4
                if scene==0 and player==0:seeds[title]=seeded
                ui+=1
    print('PASS:',ui,'original inventory/Stonehenge/dragon menus, four knights, fresh/cached panels, XP/HP/movement and inventory checks',flush=True)
    # Keep the original click and sound stack. Reload after the stat increment
    # too, so a valid 4->5 purchase must still deduct XP exactly once.
    boundaries=0
    for title in ('con-allowed','end-allowed','con-capped'):
        seed=seeds[title];field=0x48 if title.startswith('end') else 0x47
        if title=='con-capped':
            # Build with the old mapping too; disabling the fix only at the
            # click would leave the corrected cached label in place.
            seed,_=position(entry('old-capped',0,0,5,1,True,True),field)
        folder=out/('capture-'+title);folder.mkdir(exist_ok=True)
        extra=['--stat-capture',str(folder)]
        if title=='con-capped':extra+=['--nostatcapfix'] # interrupted OLD invalid purchase
        expected=run('capture-'+title,seed,100,'10:f,18:.',extra,True)
        outcome(seed[OFFSET:OFFSET+SIZE],expected,field,1)
        captures=re.findall(r'STAT-BOUNDARY index=(\d+) pc=([0-9a-f]+)',(out/('capture-'+title+'.log')).read_text())
        assert len(captures)>30 and any(int(pc,16)==0x2d0e6 for _,pc in captures)
        for index,pc_text in captures:
            pc=int(pc_text,16);checkpoint=(folder/f'mid-{int(index):02}.sav').read_bytes()
            before=seed[OFFSET:OFFSET+SIZE]
            already_incremented=get(checkpoint[OFFSET:OFFSET+SIZE],get(before,0x2fb08)+field,1)>get(before,get(before,0x2fb08)+field,1)
            gain=1 if title!='con-capped' or already_incremented else 0
            for warm in (False,True):
                name=f'resume-{title}-{index}-'+('warm' if warm else 'cold')
                # Production's warmed load runs between frames like F9. Loading
                # from inside an instruction hook would skip the restored PC's
                # hook on that first instruction and would not model real F9.
                opt=['--loadstate-at','400'] if warm else []
                after=run(name,checkpoint,500 if warm else 100,'0:.',opt)
                outcome(before,after,field,gain);boundaries+=1
    print('PASS:',boundaries,'cold/warm click, sound and payment save resumes',flush=True)
    (out/'verification.json').write_text(json.dumps(dict(native_cases=9216,ui_cases=ui,save_resumes=boundaries,runs=reports),indent=2))
if __name__=='__main__':main()
