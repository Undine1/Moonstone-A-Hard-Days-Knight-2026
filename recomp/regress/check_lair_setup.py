#!/usr/bin/env python3
"""Compare native initial-lair setup/counts with retail and resume saved copies.

The read-only local flat-module fixtures are documented in moonstone-initial-lairs.md.
Outputs, including every executable log, go to --output. No installed saves change.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe',type=Path,default=ROOT/'recomp/build/lair_setup_probe.exe')
    ap.add_argument('--evidence',type=Path,default=ROOT/'recomp/build/lair-data-20260924')
    ap.add_argument('--menu',type=Path,default=ROOT/'recomp/build/dragon-fire-fix-20260924/host/menu.sav')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    boundary=out/'boundaries';boundary.mkdir(exist_ok=True)
    data=ROOT/'dist/MoonstoneNative/data'
    for name in ('port-flat.sav','retail-flat.sav'):
        shutil.copyfile(args.evidence.resolve()/name,out/name)

    def run(name,save,extra):
        command=[str(args.probe.resolve()),'--os','--mod',str(data/'nb'),
                 '--dataset',str(data),'--diskdir',str(data),'--loadstate',str(save),
                 '--frames','0','--log',str(out/(name+'.log')),*extra]
        p=subprocess.run(command,capture_output=True,text=True,timeout=90,creationflags=0x08000000)
        (out/(name+'.txt')).write_text(p.stdout+p.stderr)
        assert p.returncode==0,(name,p.stdout,p.stderr)
        return p.stdout

    report=run('native',args.menu.resolve(),['--review-tests',str(out),'--lair-boundaries',str(boundary)])
    print(report,flush=True)
    assert 'PASS world initializers: 192' in report
    assert 'PASS encounter dispatch/count readers: 3840' in report
    assert 'PASS reader equivalence: 384' in report
    assert 'PASS guards: 138' in report
    count=0
    for line in (boundary/'boundaries.tsv').read_text().splitlines():
        name,enabled,pc,first=line.split('\t')
        run('cold-'+name,boundary/(name+'.sav'),['--lair-resume',str(out),
             *(['--nolairsetupfix'] if enabled=='0' else [])])
        for extension in ('ram','regs'):
            assert (out/('cold-result.'+extension)).read_bytes()==(boundary/(name+'.'+extension)).read_bytes(),line
        count+=1
    assert count==81,count
    result=dict(native_worlds=192,literal_full_RAM_pairs=48,old_new_exact_eight_byte_pairs=96,
                native_count_executions=15360,stat_comparisons=3840,reader_CCR_pairs=384,
                guard_cases=138,warm_boundaries=count,cold_boundaries=count,save_version=5,
                old_save_policy='Already copied nodes stay intact; only unread initial templates are corrected.')
    (out/'results.json').write_text(json.dumps(result,indent=2))
    print(f'PASS:{count} cold and warm new/legacy/A-B instruction boundaries; full RAM and CPU registers match native controls',flush=True)

if __name__=='__main__':main()
