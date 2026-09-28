#!/usr/bin/env python3
"""Check campaign choices, original combat participants and actual monster fights.

Build multiplayer_probe.c and campaign_ownership_probe.c first. Uses isolated
installs, SDL virtual devices and scratch logs/saves; game disks are read-only.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--menu',type=Path,required=True)
    ap.add_argument('--retail-image',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--before-probe',type=Path)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    data=ROOT/'dist/MoonstoneNative/data'

    def run(name,command,timeout=110):
        result=subprocess.run(command,capture_output=True,text=True,timeout=timeout,creationflags=0x08000000)
        (out/(name+'.txt')).write_text(result.stdout+result.stderr)
        assert result.returncode==0,(name,result.stdout,result.stderr)
        print(result.stdout.strip(),flush=True)

    run('native',[str(ROOT/'recomp/build/campaign_ownership_probe.exe'),
        '--fixture',str(args.menu.resolve()),'--dataset',str(data),
        '--retail-image',str(args.retail_image.resolve()),'--log',str(out/'native.log')])
    with tempfile.TemporaryDirectory(prefix='moon-campaign-ownership-') as temp:
        install=Path(temp);exe=install/'multiplayer_probe.exe'
        shutil.copyfile(ROOT/'recomp/build/SDL2.dll',install/'SDL2.dll')
        shutil.copyfile(ROOT/'recomp/controls.ini',install/'controls.ini')
        versions=([('before',args.before_probe)] if args.before_probe else [])
        versions += [('after',ROOT/'recomp/build/multiplayer_probe.exe')]
        for version,source in versions:
            shutil.copyfile(source.resolve(),exe)
            cases=[(3,1,20)] if version=='before' else [(3,1,20),(4,1,18),(2,2,21)]
            for owner,pads,node in cases:
                name=f'{version}-p{owner}-{pads}pads-node{node}'
                folder=out/name;folder.mkdir(exist_ok=True)
                command=[str(exe),'--os','--sdl','--scale','2','--mod',str(data/'nb'),
                    '--dataset',str(data),'--diskdir',str(data),
                    '--loadstate',str(args.menu.resolve()),'--probe-campaign-setup','4',str(pads),
                    '--probe-keyboard-player','4','--probe-zone-after-setup',str(owner),str(node),
                    '--probe-campaign-save',str(folder/'zone.sav'),
                    '--probe-image',str(folder/'image'),'--log',str(folder/'game.log')]
                if version=='before':command+=['--probe-zone-before']
                run(name,command)
        # Cold loading both the corrected fight and the former false prompt
        # reconstructs only its real participant, then exercises device recovery.
        for version in ('before','after'):
            save=out/f'{version}-p3-1pads-node20/zone.sav'
            if not save.is_file():continue
            name='cold-'+version;folder=out/name;folder.mkdir(exist_ok=True)
            run(name,[str(exe),'--os','--sdl','--scale','2','--mod',str(data/'nb'),
                '--dataset',str(data),'--diskdir',str(data),'--loadstate',str(save),
                '--probe-campaign-restore','4','1','4','--probe-keyboard-player','4',
                '--probe-menu-fixture',str(args.menu.resolve()),
                '--probe-campaign-save',str(folder/'restored.sav'),
                '--probe-image',str(folder/'image'),'--log',str(folder/'game.log')])

if __name__=='__main__':main()
