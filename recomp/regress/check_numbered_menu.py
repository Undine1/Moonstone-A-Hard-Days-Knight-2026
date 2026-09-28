"""Exercise the reported native numbered popup through the real SDL event loop.

Requires a copied three-choice map-popup save at the original keyboard wait.
Virtual pads are process-local; no personal log/save/config is written.
"""
import argparse,json,os,shutil,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fixture',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--exe',type=Path,default=ROOT/'recomp/build/numbered_menu_probe.exe')
    ap.add_argument('--before',action='store_true')
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=args.exe.resolve();data=ROOT/'dist/MoonstoneNative/data'
    cases=[(2,0,3)] if args.before else [(owner,mode,choice) for owner in range(4) for mode in range(3) for choice in range(1,4)]
    results=[]
    with tempfile.TemporaryDirectory(prefix='moonstone-numbered-') as tmp:
        install=Path(tmp);exe=install/'numbered_menu_probe.exe'
        shutil.copyfile(source,exe);shutil.copyfile(source.parent/'SDL2.dll',install/'SDL2.dll')
        shutil.copyfile(ROOT/'recomp/controls.ini',install/'controls.ini')
        for owner,mode,choice in cases:
            folder=out/f'p{owner+1}-mode{mode}-option{choice}';folder.mkdir(exist_ok=True)
            fixture=args.fixture.resolve()
            if owner!=2:
                # Change only the active map actor in a scratch copy. Deliberately
                # retain the previous fight pair, testing restoration from map ownership.
                b=bytearray(fixture.read_bytes());struct.pack_into('>I',b,104+0x2ebd0,0x2e7dc+owner*0x84)
                if owner==3:
                    struct.pack_into('>H',b,104+0x2e024,4)
                    struct.pack_into('>I',b,104+0x2e7dc+3*0x84+0x36,3)
                fixture=folder/'input.sav';fixture.write_bytes(b)
            cmd=[str(exe),'--os','--sdl','--scale','2','--mod',str(data/'nb'),
                 '--dataset',str(data),'--diskdir',str(data),'--loadstate',str(fixture),
                 '--log',str(folder/'game.log'),'--probe-output',str(folder),
                 '--probe-owner',str(owner),'--probe-keyboard',str(mode),'--probe-choice',str(choice)]
            if mode==0 and choice==3 and not args.before:cmd+=['--probe-recovery']
            env=os.environ.copy();env['SDL_AUDIODRIVER']='dummy';env['SDL_RENDER_DRIVER']='software'
            r=subprocess.run(cmd,capture_output=True,text=True,timeout=30,env=env,
                             creationflags=0x08000000 if os.name=='nt' else 0)
            (folder/'stdout.txt').write_text(r.stdout+r.stderr)
            results.append({'owner':owner+1,'mode':mode,'choice':choice,'exit':r.returncode,'output':r.stdout+r.stderr})
            (out/'results.json').write_text(json.dumps(results,indent=2))
            if args.before:
                assert r.returncode!=0 and 'dispatches==0' in r.stderr,(r.stdout,r.stderr)
                print('PASS frozen build reproduces premature option-two dispatch on Down',flush=True)
            else:
                assert r.returncode==0,(folder,r.stdout,r.stderr)
                print(r.stdout.strip(),flush=True)

if __name__=='__main__':main()
