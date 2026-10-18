"""Check Mudman edge pixels, complete fights and cold/warm saves.

Requires private investigation fixtures and a frozen, pre-fix observer supporting
--kind/--weak/--left-spawn. All outputs (including private saves) stay in --output.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--before-probe', type=Path, required=True)
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/mudman_spawn_probe.exe')
    ap.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--evidence', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--resume-fights', action='store_true', help='reuse completed fight evidence; continue remaining checks')
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    data = ROOT/'dist/MoonstoneNative/data'
    entry = ROOT/'dist/MoonstoneNative/pre-monkey.sav'
    probe, engine, old, old_probe = [p.resolve() for p in
                                   (args.probe, args.exe, args.before_exe, args.before_probe)]
    for exe in (probe,engine,old,old_probe):
        assert exe.is_file() and (exe.parent/'SDL2.dll').is_file(), ('missing test runtime',exe)

    def run(folder, exe, save, frames, script='0:.', extra=(), every=1):
        folder.mkdir(parents=True, exist_ok=True)
        command = [exe, '--os', '--mod', data/'nb', '--dataset', data, '--diskdir', data,
                   '--loadstate', save, '--frames', frames, '--script', script,
                   '--log', folder/'run.log', '--dumpram', folder/'final.ram',
                   '--savestate-at', frames-1, folder/'final.sav', '--wav', folder/'audio.wav',
                   '--dump', folder/'frame', '--dumpevery', every, *extra]
        p = subprocess.run(list(map(str, command)), cwd=folder, capture_output=True, text=True,
                           timeout=120, creationflags=0x08000000)
        (folder/'stdout.txt').write_text(p.stdout+p.stderr)
        assert p.returncode == 0 and 'stop=?' in p.stdout, (folder,p.stdout,p.stderr)
        return folder

    def equal_state(a, b):
        for name in ('final.ram', 'final.sav', 'audio.wav'):
            assert (a/name).read_bytes() == (b/name).read_bytes(), (a,b,name)
        assert re.search(r'unmapped=(\d+)',(a/'stdout.txt').read_text())[1] == re.search(
            r'unmapped=(\d+)',(b/'stdout.txt').read_text())[1], (a,b,'new unmapped access')

    def pictures(a, b, allow_flash):
        changes=[]
        for f in sorted(a.glob('frame_*.ppm')):
            x,y=f.read_bytes(),(b/f.name).read_bytes()
            if x==y:continue
            assert allow_flash, (f,'unexpected picture change')
            header,pixels=x.split(b'\n255\n',1); header2,pixels2=y.split(b'\n255\n',1)
            assert header==header2 and len(pixels)==len(pixels2)
            width,height=map(int,header.splitlines()[-1].split())
            assert width==320
            diffs=[i//3 for i,(v,w) in enumerate(zip(pixels,pixels2)) if v!=w]
            # Every changed pixel must be in the confirmed off-screen limb.
            assert all(291<=i%width<320 and 100<=i//width<200 for i in diffs),(f,diffs[:30])
            changes.append(dict(frame=f.name,pixels=len(set(diffs))))
        return changes

    scenarios=[('grab',False,False,'10:f,30:.'),
               ('fighting',False,False,'10:f,30:.,465:lf'),
               ('moving',False,False,'10:f,30:.,465:l,500:uf,550:r,600:df,650:.'),
               ('waves',False,True,'10:f,30:.,465:lf')]
    results=[]
    if args.resume_fights:
        results=json.loads((out/'fights.json').read_text());assert len(results)==4
    for name,left,weak,script in ([] if args.resume_fights else scenarios):
        extra=['--kind','4']+(['--left-spawn'] if left else [])+(['--weak'] if weak else [])
        a=run(out/name/'before',old_probe,entry,1600,script,extra)
        captures=out/name/'boundaries';captures.mkdir(parents=True,exist_ok=True)
        b=run(out/name/'after',probe,entry,1600,script,[*extra,'--capture',captures])
        equal_state(a,b);changes=pictures(a,b,True)
        if not left or weak:
            assert changes, (name,'reproducer did not expose flash')
        results.append(dict(name=name,changed_frames=changes,full_save_RAM_PCM_equal=True))
        print('PASS fight',name,len(changes),'edge-only pictures',flush=True)
    (out/'fights.json').write_text(json.dumps(results,indent=2))

    # Genuine left-side birth, without relying on the lair initializer's
    # alternating-side seed (the initializer resets that seed itself).
    source=(out/'grab/boundaries/constructor.sav').read_bytes()
    for face in (1,3):
        specimen=bytearray(source)
        specimen[104+0x13460e+4:104+0x13460e+6]=(-50).to_bytes(2,'big',signed=True)
        specimen[104+0x13460e+10]=face
        save=out/f'left-face-{face}.sav';save.write_bytes(specimen)
        a=run(out/f'left-face-{face}'/'before',old,save,800)
        b=run(out/f'left-face-{face}'/'after',engine,save,800)
        equal_state(a,b);pictures(a,b,False)
        print('PASS left-side birth',face,flush=True)

    controls=[('ratman',ROOT/'recomp/build/ko-fix-20260921/play/rat-fixed/input.sav','0:.'),
              ('knight-decapitation',ROOT/'recomp/build/ko-fix-20260921/play/knight-alternate-fixed/input.sav','0:.'),
              ('canopy-grab',ROOT/'dist/MoonstoneNative/choke-stab-live-2026-08-09.sav','0:.,180:df,300:.'),
              ('trogg',ROOT/'recomp/build/trogg-death-review-20261017/operator-snapshot/saves/Singleplayer/singleplayer.sav','0:l,10:.')]
    for name,save,script in controls:
        a=run(out/name/'before',old,save,800,script,every=4)
        b=run(out/name/'after',engine,save,800,script,every=4)
        equal_state(a,b);pictures(a,b,False)
        print('PASS other encounter',name,flush=True)

    saves=sorted((out/'grab/boundaries').glob('*.sav'));assert len(saves)==4
    reports=[]
    for save in saves:
        a=run(out/'saves'/save.stem/'cold',probe,save,650,every=5)
        b=run(out/'saves'/save.stem/'warm',probe,entry,750,
              extra=['--warm-fixture',save,'--warm-frame','100'],every=5)
        for name in ('final.ram','final.sav'):
            assert (a/name).read_bytes()==(b/name).read_bytes(),(save.stem,name)
        for f in a.glob('frame_*.ppm'):
            fr=int(f.stem.rsplit('_',1)[1])
            assert f.read_bytes()==(b/f'frame_{fr+100:04d}.ppm').read_bytes(),(save.stem,fr)
        with wave.open(str(a/'audio.wav')) as x,wave.open(str(b/'audio.wav')) as y:
            y.setpos(100*882);xp=x.readframes(x.getnframes());yp=y.readframes(y.getnframes())
            assert len(xp)==len(yp) and xp[256:]==yp[256:],(save.stem,'PCM after filter settles')
        # The ordinary release-path game, without observation, agrees too.
        c=run(out/'saves'/save.stem/'engine',engine,save,650,every=5)
        equal_state(a,c);pictures(a,c,False)
        reports.append(dict(boundary=save.stem,cold_warm_engine_equal=True))
        print('PASS cold/warm save',save.stem,flush=True)
    (out/'saves.json').write_text(json.dumps(reports,indent=2))
    print('PASS Mudman fights, exact nonvisual state, other deaths and save boundaries',flush=True)


if __name__=='__main__':
    main()
