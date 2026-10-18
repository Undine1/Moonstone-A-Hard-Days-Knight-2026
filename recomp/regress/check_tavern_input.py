"""Real tavern replays: quick taps, native RNG/payout timing, boundaries and saves.

Requires a private tavern-betting save; never modifies its input or the install.
Build tavern_input_probe with the canonical recipe before running.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--before', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--sdl-only', action='store_true')
    args = ap.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    data=ROOT/'dist/MoonstoneNative/data'
    if args.sdl_only:
        reports=[]
        for profile in ('development','release'):
            exe=ROOT/('recomp/build' if profile=='development' else 'recomp/build/release')/'tavern_input_sdl_probe.exe'
            cases=[(owner,keyboard,0) for owner in range(4) for keyboard in (False,True)]
            cases += [(2,False,mode) for mode in (1,2,3)]
            for owner,keyboard,mode in cases:
                name=f'{profile}-P{owner+1}-{"keyboard" if keyboard else "pad"}-mode{mode}'
                folder=out/name;folder.mkdir(exist_ok=True)
                cmd=[exe,'--fixture',args.fixture.resolve(),'--output',folder,'--owner',str(owner),
                     '--mode',str(mode),'--os','--sdl','--dataset',data,'--diskdir',data,'--log',folder/'run.log']
                if keyboard:cmd+=['--keyboard']
                r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=35,creationflags=0x08000000)
                (folder/'stdout.txt').write_text(r.stdout+r.stderr)
                assert r.returncode==0 and 'PASS SDL' in r.stdout,(name,r.stdout,r.stderr)
                reports.append(dict(case=name,result=r.stdout.strip()))
                print('PASS',name,flush=True)
        (out/'sdl-results.json').write_text(json.dumps(reports,indent=2)+'\n')
        return
    probe=ROOT/'recomp/build/tavern_input_probe.exe'
    current=ROOT/'recomp/build/moonstone.exe'
    initial=args.fixture.read_bytes();offset=20+4*struct.unpack_from('<I',initial,16)[0]
    results=[]

    def fixture(name, stake=3, gold=136, seed=0xc88f):
        b=bytearray(initial)
        def word(at,v):struct.pack_into('>H',b,offset+at,v)
        actor=struct.unpack_from('>I',b,offset+0x42f66)[0]
        word(0x392d4,280);word(0x392d6,[48,76,104,131,157,186][stake-1])
        word(actor+0x4a,gold);struct.pack_into('>I',b,offset+0x391a8,seed)
        path=out/(name+'.sav');path.write_bytes(b);return path

    def run(name, save, script='0:.', frames=220, exe=probe, extra=()):
        p=out/name;p.mkdir(exist_ok=True)
        cmd=[exe,'--os','--mod',data/'nb','--dataset',data,'--diskdir',data,
             '--loadstate',save,'--frames',str(frames),'--script',script,'--log',p/'run.log',
             '--dumpram',p/'ram.bin','--wav',p/'audio.wav','--dump',p/'frame.ppm',
             '--savestate-at',str(frames-1),p/'end.sav',*extra]
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,
                         timeout=90,creationflags=0x08000000)
        (p/'stdout.txt').write_text(r.stdout+r.stderr)
        assert r.returncode==0 and 'unmapped=0' in r.stdout,(name,r.stdout,r.stderr)
        log=(p/'run.log').read_text(errors='replace')
        events=re.findall(r'^TI (?:BET|ROLL|FACES).*',log,re.M)
        summary=re.search(r'TI SUMMARY .*',r.stdout)
        meta={k:int(v) for k,v in re.findall(r'(polls|bets|rolls|rng|acks|exits|injections)=(\d+)',summary[0])} if summary else {}
        record=dict(name=name,events=events,**meta);results.append(record)
        return p,record

    def equal_files(a,b,names):
        for name in names:assert (a/name).read_bytes()==(b/name).read_bytes(),(a,b,name)

    base=fixture('base')
    reference,ref=run('native-poll-reference',base,'16:f,17:.',extra=['--unfixed'])
    assert ref['bets']==ref['rolls']==1 and ref['injections']==0
    for length in (1,5,15):
        for start in range(3,17):
            name=f'tap-{start}-{length}'
            p,r=run(name,base,f'{start}:f,{start+length}:.')
            assert r['bets']==r['rolls']==1 and r['acks']==0,(name,r)
            assert r['events']==ref['events'] and r['rng']==ref['rng'],(name,'RNG/timing',r,ref)
            # The physical press occurred on a different frame: the native
            # cursor branch can shift the first sound's sub-frame start.
            # Compare full PCM only for identical-input before/after controls.
            equal_files(p,reference,('ram.bin','frame.ppm'))
        print('PASS all 14 input phases, duration',length,'frame(s): same native roll/RNG/payout',flush=True)

    for stake in range(1,6):
        for seed in (0xfffffde2,0xc88f,0xfffffffb,0xacfb):
            s=fixture(f'stake-{stake}-seed-{seed:x}',stake,136,seed)
            p,r=run(f'fixed-{stake}-{seed:x}',s,'3:f,4:.')
            q,t=run(f'reference-{stake}-{seed:x}',s,'16:f,17:.',extra=['--unfixed'])
            assert r['events']==t['events'] and r['rng']==t['rng'] and r['bets']==1
            equal_files(p,q,('ram.bin','frame.ppm'))
    print('PASS five stakes x four original seed choices, identical native RNG/results/payouts',flush=True)

    for gold in (0,1,2):
        s=fixture(f'poor-{gold}',3,gold)
        _,r=run(f'unaffordable-{gold}',s,'3:f,4:.,6:f,7:.,9:f,10:.')
        assert r['bets']==r['rolls']==0,r
    _,r=run('rapid-taps',base,'3:f,4:.,5:f,6:.,7:f,8:.,9:f,10:.')
    assert r['bets']==r['rolls']==1 and r['acks']==0,r
    _,r=run('hold-through-result',base,'3:f,180:.',frames=350)
    assert r['bets']==r['rolls']==r['acks']==1,r
    _,r=run('result-ack-only',base,'3:f,4:.,170:f,171:.',frames=350)
    assert r['bets']==r['rolls']==r['acks']==1,r
    _,r=run('next-intentional-bet',base,'3:f,4:.,170:f,171:.,210:f,211:.',frames=400)
    assert r['bets']==r['rolls']==2 and r['acks']==1,r
    _,r=run('pointer-changed-row',base,'3:f,4:u,16:.',frames=220)
    assert r['bets']==r['rolls']==0,r
    exit_save=fixture('exit',6)
    _,r=run('quick-exit',exit_save,'3:f,4:.',frames=120)
    assert r['bets']==r['rolls']==0 and r['exits']==1,r
    print('PASS unaffordable/rapid/held inputs, result acknowledgement, next round, pointer movement and Exit',flush=True)

    # No input and previously accepted inputs must remain byte-for-byte neutral.
    for name,script in [('idle','0:.'),('accepted','3:f,20:.,180:f,185:.,220:f,240:.'),
                         ('exit-held','3:f,20:.')]:
        s=exit_save if name=='exit-held' else base
        p,_=run('unchanged-before-'+name,s,script,500,args.before.resolve())
        q,_=run('unchanged-after-'+name,s,script,500,current)
        equal_files(p,q,('ram.bin','audio.wav','frame.ppm','end.sav'))
    print('PASS unchanged accepted input/idle/exit: complete save, RAM, PCM and pixels identical',flush=True)

    # Pending physical input is intentionally not saved. Restoring must not
    # resurrect it; a new tap after restoring must still work normally.
    pending,_=run('pending-save',base,'3:f,4:.',7)
    for name,script,count in [('idle','0:.',0),('fresh','2:f,3:.',1)]:
        _,r=run('cold-pending-'+name,pending/'end.sav',script)
        assert r['bets']==count,r
    _,r=run('warm-discards-pending',base,'3:f,4:.',extra=['--warm','7',pending/'end.sav'])
    assert r['bets']==0,r
    boundary=out/'injected.sav'
    run('capture-injected',base,'3:f,4:.',extra=['--capture-injection',boundary])
    for name,extra in [('cold',()),('warm',('--warm','0',boundary))]:
        _,r=run(name+'-injected',boundary,extra=extra)
        assert r['bets']==r['rolls']==1 and r['injections']==0,r
    print('PASS cold/warm loads before and after native input consumption; no stale/double bets',flush=True)
    assert args.fixture.read_bytes()==initial
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS',len(results),'actual tavern replays; source save preserved',flush=True)

if __name__=='__main__':main()
