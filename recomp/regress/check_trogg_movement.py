"""Compare recoil movement, unchanged fights and old-save resumes.

Private fixtures and replay observations are read-only inputs. No installation
or user save is modified. Build trogg_movement_{fight_,}probe.c first.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

ROOT = Path(__file__).resolve().parents[2]

def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--observations', type=Path, required=True, help='Full-delay investigation directory')
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/trogg_movement_fight_probe.exe')
    ap.add_argument('--native-images', type=Path, help='Prepared native cases.tsv and earlier/retail RAM images')
    ap.add_argument('--native-probe', type=Path, default=ROOT/'recomp/build/trogg_movement_probe.exe')
    ap.add_argument('--saves-only', action='store_true', help='Resume verification after completed replay comparisons')
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    source = args.observations.resolve()
    results = json.loads((out/'replays.json').read_text()) if args.saves_only else []
    if args.native_images:
        p=subprocess.run([str(args.native_probe.resolve()),'--native',str(args.native_images.resolve()),
                          '--log',str(out/'native.log')],capture_output=True,text=True,
                         timeout=300,creationflags=0x08000000)
        (out/'native.stdout').write_text(p.stdout+p.stderr)
        assert p.returncode==0 and 'complete RAM/CPU oracle agreement' in p.stdout,p.stdout+p.stderr
        print(p.stdout.strip(),flush=True)

    def run(cmd, folder):
        p = subprocess.run(cmd, cwd=folder, capture_output=True, text=True,
                           timeout=90, creationflags=0x08000000)
        (folder/'stdout.txt').write_text(p.stdout+p.stderr)
        assert p.returncode == 0 and 'unmapped=0' in p.stdout, (folder, p.stdout, p.stderr)
        return p.stdout

    for row in ([] if args.saves_only else json.loads((source/'runs.json').read_text())):
        folder = out/row['name']; folder.mkdir(exist_ok=True)
        cmd = list(row['command']); cmd[0] = str(args.probe.resolve())
        if '--capture-dir' in cmd:
            i=cmd.index('--capture-dir'); del cmd[i:i+2]
        for flag, name in (('--log','probe.log'),('--dumpram','probe.ram')):
            cmd[cmd.index(flag)+1]=str(folder/name)
        cmd += ['--wav',str(folder/'probe.wav'),'--savestate-at','649',str(folder/'probe.sav')]
        capture=row['name'] in ('axe-0rf-d','axe-0uf-d','axe-0lf-away')
        if capture:cmd += ['--dump',str(folder/'probe-frame'),'--dumpevery','5']
        stdout=run(cmd,folder)
        m=re.search(r'FULL summary hit=(-?\d+) poll=(-?\d+) move=(-?\d+) total=(-?\d+)',stdout)
        assert m, row['name']
        after=dict(zip(('hit','poll','move','total'),map(int,m.groups())))
        assert after['hit']==row['hit'] and after['poll']==row['poll'], (row['name'],'attack/recovery changed')
        if row['species']=='axe' and row['name'] in ('axe-0rf-d','axe-0uf-d','axe-0l-15lf-d','axe-0l-15lf-l'):
            assert after['move']==after['poll'] and after['total']<row['total'], (row['name'],after)
        if row['name'] in ('axe-0rf-r','axe-0lf-away','axe-0lf-r','axe-0uf-r'):
            assert after['move']==row['move'], (row['name'],'ordinary obstruction changed')
        # Reproduce the same ordinary inputs through the actual engine: the
        # observer must not cause the measured improvement or any other drift.
        i=cmd.index('--after-hit');del cmd[i:i+2]
        log=(folder/'probe.log').read_text()
        events=[fields(l) for l in log.splitlines() if l.startswith('FULL requested ')]
        if events:
            event=events[0]
            keys=''.join(k for k in ('r','l','u','d','f') if int(event[k])) or '.'
            cmd[cmd.index('--script')+1]+=','+event['fr']+':'+keys
        commands=[]
        for label,exe in (('after',args.exe),('before',args.before_exe)):
            cmd[0]=str(exe.resolve())
            for flag,name in (('--log',f'{label}.log'),('--dumpram',f'{label}.ram'),('--wav',f'{label}.wav')):
                cmd[cmd.index(flag)+1]=str(folder/name)
            cmd[cmd.index('--savestate-at')+2]=str(folder/(label+'.sav'))
            if capture:cmd[cmd.index('--dump')+1]=str(folder/(label+'-frame'))
            run(cmd,folder);commands.append(list(cmd))
        assert (folder/'after.ram').read_bytes()==(folder/'probe.ram').read_bytes(), (row['name'],'observer drift')
        for suffix in ('sav','wav'):
            assert (folder/f'after.{suffix}').read_bytes()==(folder/f'probe.{suffix}').read_bytes(),(row['name'],'observer '+suffix)
        same=(folder/'before.ram').read_bytes()==(folder/'after.ram').read_bytes()
        if row['species']!='axe':
            assert same, (row['name'],'other enemy changed')
            for suffix in ('sav','wav'):
                assert (folder/f'before.{suffix}').read_bytes()==(folder/f'after.{suffix}').read_bytes(),(row['name'],suffix)
        if capture:
            for p in folder.glob('before-frame_*.ppm'):
                frame=int(p.stem.rsplit('_',1)[1])
                if frame<after['poll']:
                    assert p.read_bytes()==(folder/p.name.replace('before-','after-')).read_bytes(),(row['name'],'pre-movement picture')
        results.append(dict(name=row['name'],before={k:row[k] for k in after},after=after,
                            identical_ram=same,
                            identical_save=(folder/'before.sav').read_bytes()==(folder/'after.sav').read_bytes(),
                            identical_audio=(folder/'before.wav').read_bytes()==(folder/'after.wav').read_bytes(),commands=commands))
        print('PASS',row['name'],row['total'],'->',after['total'],'identical' if same else 'movement differs',flush=True)
        (out/'replays.json').write_text(json.dumps(results,indent=2))

    # Holding attack cannot invoke this movement-only correction. Compare the
    # entire fights, including all damage, animation/audio and eventual death.
    template=next(r for r in results if r['name']=='axe-0rf-d')['commands'][0]
    for n,script in enumerate(() if args.saves_only else ('0:rf','0:lf','0:uf','0:l,15:lf')):
        folder=out/f'attack-only-{n}';folder.mkdir(exist_ok=True)
        for label,exe in (('before',args.before_exe),('after',args.exe)):
            cmd=list(template);cmd[0]=str(exe.resolve());cmd[cmd.index('--script')+1]=script
            for flag,name in (('--log',f'{label}.log'),('--dumpram',f'{label}.ram'),('--wav',f'{label}.wav'),('--dump',f'{label}-frame')):
                cmd[cmd.index(flag)+1]=str(folder/name)
            cmd[cmd.index('--savestate-at')+2]=str(folder/(label+'.sav'))
            run(cmd,folder)
        for before in folder.glob('before.*'):
            if before.suffix in ('.ram','.sav','.wav'):
                assert before.read_bytes()==(folder/('after'+before.suffix)).read_bytes(), (folder,before.name)
        pictures=list(folder.glob('before-frame_*.ppm'));assert len(pictures)==130
        for p in pictures:assert p.read_bytes()==(folder/p.name.replace('before-','after-')).read_bytes(), (folder,p.name)
        print('PASS attack-only',script,'650 frames: RAM, save, PCM and 130 pictures identical',flush=True)

    # Original pre-fix saves, including a finished recoil still displaying its
    # last pose. Warm-load from an already running fight, using the same loader
    # as F9. No intro warmup or persistent test input state crosses the restore.
    snapshots=sorted((source/'reaction-axe-0rf-d/captures').glob('*-input-*.sav'))[:5]
    snapshots+=sorted((source/'reaction-axe-0uf-d/captures').glob('*-input-*.sav'))[:5]
    # Also create new saves with the actual candidate, while both the knight's
    # recovery and the Trogg's longer recoil are in progress. F5 uses this same
    # serializer. Each new snapshot then gets both cold and warm continuations.
    new_saves=[]
    for case,frames in (('axe-0rf-d',(32,38,43,48)),('axe-0uf-d',(40,50,60,65))):
        entry=next(r for r in results if r['name']==case)['commands'][0]
        for frame in frames:
            folder=out/f'new-save-{case}-{frame}';folder.mkdir(exist_ok=True)
            save=folder/'recoil.sav';cmd=list(entry)
            cmd[cmd.index('--frames')+1]=str(frame+1)
            cmd[cmd.index('--savestate-at')+1]=str(frame);cmd[cmd.index('--savestate-at')+2]=str(save)
            for flag,name in (('--log','game.log'),('--dumpram','final.ram'),('--wav','sound.wav'),('--dump','frame')):
                cmd[cmd.index(flag)+1]=str(folder/name)
            run(cmd,folder);assert save.is_file();snapshots.append(save);new_saves.append(str(save))
    for index,save in enumerate(snapshots):
        folder=out/f'old-save-{index}';folder.mkdir(exist_ok=True)
        runs=[]
        for warm in (False,True):
            label='warm' if warm else 'cold';cmd=list(template)
            cmd[0]=str((args.probe if warm else args.exe).resolve());cmd[cmd.index('--loadstate')+1]=str(save)
            # Both continue from the exact same CPU/RAM state and held Down.
            # Host frame numbering differs; only game RAM is compared.
            if warm:cmd += ['--warm-fixture',str(save),'--warm-frame','100']
            cmd[cmd.index('--frames')+1]=str(200 if warm else 100)
            cmd[cmd.index('--script')+1]='0:d'
            i=cmd.index('--savestate-at');del cmd[i:i+3]
            for flag,name in (('--log',f'{label}.log'),('--dumpram',f'{label}.ram'),('--wav',f'{label}.wav'),('--dump',f'{label}-frame')):
                cmd[cmd.index(flag)+1]=str(folder/name)
            run(cmd,folder);runs.append(cmd)
        assert (folder/'warm.ram').read_bytes()==(folder/'cold.ram').read_bytes(), (save,'warm/cold game drift')
        print('PASS save cold/warm continuation',save.parent.name,save.name,flush=True)
    (out/'verification.json').write_text(json.dumps(dict(replays=results,attack_only=4,old_save_pairs=10,
        new_save_pairs=len(new_saves),new_saves=new_saves),indent=2))

if __name__=='__main__':main()
