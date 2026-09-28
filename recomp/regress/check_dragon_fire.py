#!/usr/bin/env python3
"""Verify dragon-fire death timing against original scripts and saved boundaries.

Uses the documented animation-data investigation fixtures read only. Every game
process has an explicit scratch log; no installed game file is a write target.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
DATA=ROOT/'dist/MoonstoneNative/data'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe',type=Path,default=ROOT/'recomp/build/dragon_fire_probe.exe')
    ap.add_argument('--evidence',type=Path,default=ROOT/'recomp/build/animation-data-20260924')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    evidence=args.evidence.resolve();probe=args.probe.resolve()

    def run(name,save,extra=(),frames=100,data=DATA,exe=probe):
        folder=out/name;folder.mkdir(exist_ok=True)
        cmd=[str(exe),'--os','--mod',str(data/'nb'),'--dataset',str(data),'--diskdir',str(data),
             '--loadstate',str(save),'--frames',str(frames),'--log',str(folder/'run.log'),
             '--dumpram',str(folder/'final.ram'),*extra]
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=60,creationflags=0x08000000)
        (folder/'stdout.txt').write_text(p.stdout+p.stderr)
        assert p.returncode==0,(name,p.stdout,p.stderr)
        return folder

    # Actual native interpreters, including all animation commands. The reference
    # fixtures stub only audio/blits as documented in compare_native.py.
    reports=[]
    for hp in (23,0,-7):
        for facing in (1,3):
            for gore in (0,1):
                tag=f'hp{hp}-f{facing}-g{gore}'
                current=evidence/'native'/('port-'+tag)/'input.sav'
                retail=evidence/'native'/('retail-'+tag)/'input.sav'
                folders={}
                for mode,save,flags,data in [('fixed',current,[],DATA),
                        ('disabled',current,['--nodragonfirefix'],DATA),
                        ('retail',retail,[],evidence/'retail-data')]:
                    folders[mode]=run(mode+'-'+tag,save,
                        ['--animation-oracle','--animation-limit','460',*flags],data=data)
                def trace(folder):
                    log=(folder/'run.log').read_text()
                    assert 'animation oracle complete' in log and 'unmapped=0' in log
                    return [dict(re.findall(r'(\w+)=([^ ]*)',s)) for s in log.splitlines() if s.startswith('ANIM-END')]
                values={k:trace(v) for k,v in folders.items()}
                # Cursor addresses and temporary repeat flags differ with the
                # adapted earlier layout; visible poses and native outcomes must match.
                keys=('parts','bank','actorbank','active','hp')
                reduced=lambda rows:[tuple(r[k] for k in keys) for r in rows]
                assert reduced(values['fixed'])==reduced(values['retail']),tag
                old=json.loads((evidence/'native'/('port-'+tag)/'result.json').read_text())
                assert reduced(values['disabled'])==reduced(old['trace'])
                assert (folders['disabled']/'final.ram').read_bytes()==(evidence/'native'/('port-'+tag)/'final.ram').read_bytes()
                expected=11 if hp>0 else 416
                assert len(values['fixed'])==expected
                # A captured script whose count is already1 must also roll back
                # at its next native count read when A/B is requested.
                already=bytearray(current.read_bytes());already[104+0x3204f]=1
                saved=out/('already-'+tag+'.sav');saved.write_bytes(already)
                rollback=run('rollback-'+tag,saved,['--animation-oracle','--animation-limit','460','--nodragonfirefix'])
                if hp<=0:assert (rollback/'final.ram').read_bytes()==(folders['disabled']/'final.ram').read_bytes()
                reports.append({'case':tag,'retail_updates':expected,'native_pose_and_outcomes_match':True,
                                'disabled_full_ram_matches_original':True})
    print('PASS:48 original/fixed/retail/rollback executions;12 configurations, zero unmapped reads',flush=True)

    boundary=out/'boundaries';boundary.mkdir(exist_ok=True)
    source=evidence/'native/port-hp0-f1-g0/input.sav'
    warm=run('warm-tests',source,['--fire-tests',str(source),'--fire-output',str(boundary)],frames=0)
    stdout=(warm/'stdout.txt').read_text();print(stdout,flush=True)
    assert 'PASS:64 native reader/control pairs' in stdout and 'PASS:43 disabled' in stdout
    count=0
    for line in (boundary/'boundaries.tsv').read_text().splitlines():
        name,want,*metadata=line.split('\t')
        resumed=run('cold-'+name[:-4],boundary/name,['--fire-resume',str(out/'cold-result.ram')],frames=0)
        assert (out/'cold-result.ram').read_bytes()==(boundary/('expected-'+want+'.ram')).read_bytes(),line
        count+=1
    print(f'PASS:{count} cold native instruction-boundary restores match corresponding uninterrupted RAM',flush=True)
    result={'native_matrix':reports,'native_executions':48,'native_read_pairs':64,'inactive_guards':43,
            'warm_boundaries':count,'cold_boundaries':count,'save_format':5,
            'legacy_policy':'A loop already initialized before loading finishes its existing native countdown.'}
    (out/'results.json').write_text(json.dumps(result,indent=2))

if __name__=='__main__':main()
