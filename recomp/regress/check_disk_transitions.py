"""Run real SDL P2 town/AI-attack disk transitions in isolated installs."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--probe-exe',type=Path,required=True)
    ap.add_argument('--before-exe',type=Path)
    ap.add_argument('--map',type=Path,required=True)
    ap.add_argument('--tower',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--case',default='tower,attack')
    ap.add_argument('--keyboard-p2',action='store_true')
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    data=ROOT/'dist/MoonstoneNative/data'
    with tempfile.TemporaryDirectory(prefix='moonstone-transitions-') as tmp:
        install=Path(tmp);exe=install/'transition_sdl_probe.exe'
        shutil.copyfile(ROOT/'recomp/build/SDL2.dll',install/'SDL2.dll')
        shutil.copyfile(ROOT/'recomp/controls.ini',install/'controls.ini')
        versions=([('before',args.before_exe)] if args.before_exe else [])+[('after',args.probe_exe)]
        for version,source in versions:
            shutil.copyfile(source.resolve(),exe)
            for name,fixture,flags in [('tower',args.tower,[]),('attack',args.map,['--probe-attack']),
                                       ('prepare',args.tower,['--prepare-attack'])]:
                if name not in args.case.split(','):continue
                if name=='prepare' and version=='before':continue
                folder=out/(name+'-'+version);folder.mkdir(exist_ok=True)
                cmd=[str(exe),'--os','--sdl','--scale','2','--mod',str(data/'nb'),
                     '--dataset',str(data),'--diskdir',str(data),'--loadstate',str(fixture.resolve()),
                     '--log',str(folder/'game.log'),'--probe-output',str(folder),*flags]
                if version=='before':cmd+=['--expect-old']
                if args.keyboard_p2:cmd+=['--keyboard-p2']
                r=subprocess.run(cmd,capture_output=True,text=True,timeout=90 if name=='prepare' else 45,creationflags=0x08000000)
                (folder/'stdout.txt').write_text(r.stdout+r.stderr)
                assert r.returncode==0,(version,name,r.stdout,r.stderr)
                print(r.stdout.strip(),flush=True)

if __name__=='__main__':main()
