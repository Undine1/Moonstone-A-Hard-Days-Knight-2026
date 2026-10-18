#!/usr/bin/env python3
"""Normal error reporting must survive removing developer tools.

Requires the release-profile host_error_probe. Only its dialogs/audio device
and renderer are substituted; the real public parser/setup/error handlers run.
All writes go to a NEW scratch directory. The full data suite runs separately.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe',type=Path,default=ROOT/'recomp/build/release/host_error_probe.exe')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=False)
    data=ROOT/'dist/MoonstoneNative/data'
    base=[str(args.probe.resolve()),'--dataset',str(data),'--diskdir',str(data)]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    results=[]
    cases=[('audio',[],0,'Injected audio-open failure'),
           ('video',[],1,'SDL_Init'),
           ('invalid-profile',['--save-profile','typo'],2,'Missing or invalid save profile'),
           ('missing-profile',['--save-profile'],2,'Missing or invalid save profile')]
    for name, flags, code, expected in cases:
        log=out/f'{name}.log'
        case_env=dict(env)
        if name=='video': case_env['SDL_VIDEODRIVER']='moonstone-nonexistent-test-driver'
        # Log before the invalid selector so the missing-value case is genuine.
        r=subprocess.run(base+['--log',str(log)]+flags,env=case_env,capture_output=True,text=True,
                         timeout=15,creationflags=0x08000000)
        text=log.read_text()
        assert r.returncode==code and expected in text, (name,r.returncode,text,r.stderr)
        assert 'TEST DIALOG Moonstone:' in r.stderr and expected in r.stderr
        assert 'PROCESS: x64, 64-bit' in text and 'OS:' in text and 'OS ARCH:' in text
        assert 'MOONSTONE_BUILD_PROFILE:release' in text
        results.append(f'PASS public {name}: correct exit, dialog, log and OS/architecture');print(results[-1],flush=True)
    logdir=out/'not-a-log-file';logdir.mkdir()
    r=subprocess.run(base+['--log',str(logdir)],env=env,capture_output=True,text=True,
                     timeout=15,creationflags=0x08000000)
    assert r.returncode==0 and "couldn't create its diagnostic log" in r.stderr, r.stderr
    assert 'TEST DIALOG Moonstone:' in r.stderr and 'SETUP RESULT: ready' in r.stderr
    assert 'OS ARCH:' in r.stderr
    results.append('PASS public log-open failure: warning, stderr fallback and continued startup');print(results[-1])
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__': main()
