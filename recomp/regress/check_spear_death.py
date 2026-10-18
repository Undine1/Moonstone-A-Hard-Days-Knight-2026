"""Spear execution: actual stuck save, controlled contact, other deaths, reloads.

Private evidence and generated saves are never part of the public package.
Build spear_death_probe in both profiles before running this check.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[2]
HERO, RAM_OFFSET = 0x2e7dc, 104


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--before-probe', type=Path,
                    help='Same observer compiled against the frozen preceding moon.c')
    ap.add_argument('--saves-only', action='store_true')
    args = ap.parse_args()
    evidence = args.evidence.resolve()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    data, fixture = evidence/'private-data', evidence/'stuck-spearmen.sav'
    before, after = evidence/'before.exe', ROOT/'recomp/build/moonstone.exe'
    probe = ROOT/'recomp/build/spear_death_probe.exe'

    def execute(cmd, folder):
        r = subprocess.run(list(map(str, cmd)), cwd=folder, capture_output=True,
                           text=True, timeout=120, creationflags=0x08000000)
        (folder/'stdout.txt').write_text(r.stdout+r.stderr)
        assert r.returncode == 0, (folder, r.returncode, r.stdout, r.stderr)
        return r.stdout

    def run(name, exe, save=fixture, frames=1200, script='0:.', extra=(), every=20):
        folder = out/name; folder.mkdir(parents=True, exist_ok=True)
        cmd = [exe, '--os', '--mod', data/'nb', '--dataset', data, '--diskdir', data,
               '--loadstate', save, '--frames', frames, '--script', script,
               '--log', folder/'run.log', '--dumpram', folder/'final.ram',
               '--savestate-at', frames-1, folder/'final.sav', '--wav', folder/'audio.wav',
               '--dump', folder/'frame', '--dumpevery', every, *extra]
        stdout = execute(cmd, folder)
        assert 'unmapped=0' in stdout, (folder, stdout)
        return folder

    def equal(a, b):
        files = ['final.ram', 'final.sav', 'audio.wav']
        files += [p.name for p in a.glob('frame_*.ppm')]
        assert len(files)>3
        for name in files:
            assert (a/name).read_bytes() == (b/name).read_bytes(), (a, b, name)

    def death(folder):
        ram = (folder/'final.ram').read_bytes()
        assert ram[HERO+0x49] == 0, (folder, 'one life lost')
        assert int.from_bytes(ram[HERO+0x50:HERO+0x52], 'big') == 90, (folder, 'HP restored')
        assert ram[0x2e0bc+8] == 0, (folder, 'fight ended')
        log = (folder/'run.log').read_text()
        assert len(re.findall(r'^SPEAR-TEST life ', log, re.M)) == 1, (folder, 'life handler count')

    if not args.saves_only:
        for profile in ('development', 'release'):
            path = ROOT/('recomp/build' if profile=='development' else 'recomp/build/release')/'spear_death_probe.exe'
            folder = out/('native-'+profile); folder.mkdir(exist_ok=True)
            originals = ROOT/'recomp/build/tree-spawn-review-20261017'
            print(execute([path, '--native', originals/'port.ram', originals/'retail.ram',
                           '--log', folder/'run.log'], folder).strip(), flush=True)

        # Reuse preserved baseline/canonical runs where available; the observer
        # must match the canonical executable in complete state, pixels and PCM.
        a, b, c = (evidence/n for n in ('baseline', 'recovered', 'observer'))
        equal(b, c); death(c)
        old = (a/'final.ram').read_bytes()
        assert old[HERO+0x49] == 1 and old[HERO+0x50:HERO+0x52] == b'\0\0'
        assert old[0x2e0bc+8] == 1
        assert 'root=0 recovery=1 lives=1' in (c/'stdout.txt').read_text()
        print('PASS exact user save: baseline stuck; recovery/observer normal death, one life', flush=True)

        boundaries = out/'boundaries'; boundaries.mkdir(exist_ok=True)
        seed = out/'prefatal.sav'
        seedrun = run('seed', probe, extra=['--seed', seed, '--captures', boundaries])
        death(seedrun)
        assert 'root=1 recovery=0 lives=1' in (seedrun/'stdout.txt').read_text()
        a = run('fatal-before', before, seed)
        b = run('fatal-after', after, seed)
        c = run('fatal-observer', probe, seed)
        equal(b, c); death(c)
        old = (a/'final.ram').read_bytes()
        assert old[HERO+0x49] == 1 and old[0x2e0bc+8] == 1
        assert 'root=1 recovery=0 lives=1' in (c/'stdout.txt').read_text()
        print('PASS controlled fatal contact: native bug reproduced and prevented, observer identical', flush=True)

        controls = [
            ('rat-paired', ROOT/'recomp/build/ko-fix-20260921/play/rat-fixed/input.sav', '0:.', 600),
            ('knight-ordinary', ROOT/'recomp/build/ko-fix-20260921/play/knight-fixed/input.sav',
             ','.join(f'{i}:lf,{i+25}:.' for i in range(0,250,50)), 600),
            ('knight-decapitation', ROOT/'recomp/build/ko-fix-20260921/play/knight-alternate-fixed/input.sav', '0:.', 600),
            ('canopy-death', ROOT/'dist/MoonstoneNative/choke-stab-live-2026-08-09.sav', '0:.,180:df,300:.', 600),
            ('canopy-living-escape', ROOT/'dist/MoonstoneNative/choke-stab-live-2026-08-09.sav', '0:df,80:.', 600),
            ('axe-trogg-death', ROOT/'recomp/build/trogg-death-review-20261017/operator-snapshot/saves/Singleplayer/singleplayer.sav', '0:l,10:.', 3200),
        ]
        reports=[]
        for name, save, script, frames in controls:
            a=run(name+'/before',before,save,frames,script,every=10)
            b=run(name+'/after',after,save,frames,script,every=10)
            equal(a,b)
            reports.append(dict(name=name, frames=frames, full_save_RAM_PCM_pixels_identical=True))
            print('PASS unchanged',name,flush=True)
        (out/'controls.json').write_text(json.dumps(reports,indent=2))

    # Restore each original transition both into a new process and a running
    # fight. Host output-filter settling is not serialized; bound it separately.
    saves=sorted((out/'boundaries').glob('*.sav'))
    assert len(saves)==5, [p.name for p in saves]
    reports=[]
    for save in [fixture, *saves]:
        a=run('resumes/'+save.stem+'/cold',probe,save,600,every=10)
        b=run('resumes/'+save.stem+'/warm',probe,fixture,700,every=10,
              extra=['--warm',save,'--warm-frame','100'])
        assert (a/'final.ram').read_bytes()==(b/'final.ram').read_bytes(),save.stem
        cold=(a/'final.sav').read_bytes(); warm=(b/'final.sav').read_bytes()
        full_equal=cold==warm
        if not full_equal:
            # A cold Musashi reset has a different initial cycle budget from
            # a warmed CPU. At the late, silent life-loss boundary this leaves
            # only cumulative counters / an expired blitter deadline shifted.
            # Require the PREVIOUS build to reproduce both complete results;
            # do not mask a newly introduced state difference as harmless.
            assert args.before_probe and save.stem=='before-life-loss'
            assert cold[:RAM_OFFSET+0x200000]==warm[:RAM_OFFSET+0x200000]
            allowed=set(range(2098040,2098056))|set(range(2098120,2098128))
            assert all(i in allowed for i,(x,y) in enumerate(zip(cold,warm)) if x!=y)
            oldcold=run('resumes/'+save.stem+'/old-cold',args.before_probe.resolve(),save,600,every=10)
            oldwarm=run('resumes/'+save.stem+'/old-warm',args.before_probe.resolve(),fixture,700,every=10,
                        extra=['--warm',save,'--warm-frame','100'])
            equal(a,oldcold)
            # Before frame100 the previous build is deliberately still stuck;
            # compare the final save/RAM and post-load pictures, not that prefix.
            for name in ('final.ram','final.sav'):
                assert (b/name).read_bytes()==(oldwarm/name).read_bytes(),(save.stem,name)
            for p in b.glob('frame_*.ppm'):
                if int(p.stem.rsplit('_',1)[1])>=100:
                    assert p.read_bytes()==(oldwarm/p.name).read_bytes(),p.name
        for p in a.glob('frame_*.ppm'):
            fr=int(p.stem.rsplit('_',1)[1])
            assert p.read_bytes()==(b/f'frame_{fr+100:04d}.ppm').read_bytes(),(save.stem,fr)
        with wave.open(str(a/'audio.wav')) as x,wave.open(str(b/'audio.wav')) as y:
            y.setpos(100*882);xp=x.readframes(x.getnframes());yp=y.readframes(y.getnframes())
            assert len(xp)==len(yp)
            differing=[i for i,(u,v) in enumerate(zip(xp,yp)) if u!=v]
            settling=1+max(differing)//4 if differing else 0
            assert settling<=64 and xp[256:]==yp[256:],(save.stem,settling)
        death(a); death(b)
        reports.append(dict(boundary=save.stem,cold_warm_RAM_CPU_pixels_identical=True,
                            complete_saves_identical=full_equal,
                            existing_counter_difference_verified=not full_equal,
                            output_filter_settling_samples=settling,one_life=True))
        print('PASS cold/warm save',save.stem,flush=True)
    (out/'saves.json').write_text(json.dumps(reports,indent=2))
    print('PASS spear execution/recovery, other deaths and save lifecycle',flush=True)


if __name__=='__main__':
    main()
