"""Reproduce the interrupted knight fall; preserve other deaths and saved states.

Local investigation fixtures are read-only. All generated files stay in output.
Build knight_fall_probe.c first; neither probe nor private inputs may be shipped.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[2]
RAM_OFFSET, RAM_SIZE, HERO = 104, 0x200000, 0x2e7dc


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--before-exe', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'recomp/build/moonstone.exe')
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/knight_fall_probe.exe')
    ap.add_argument('--evidence', type=Path, default=ROOT/'recomp/build/trogg-death-review-20261017')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--resume-saves', action='store_true')
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    evidence = args.evidence.resolve()
    before, after, probe = [p.resolve() for p in (args.before_exe, args.exe, args.probe)]
    data = ROOT/'recomp/build/hammer-hit-review-20261017/private-data'

    def execute(cmd, folder):
        p = subprocess.run(list(map(str, cmd)), cwd=folder, capture_output=True, text=True,
                           timeout=120, creationflags=0x08000000)
        (folder/'stdout.txt').write_text(p.stdout+p.stderr)
        assert p.returncode == 0, (folder, p.stdout, p.stderr)
        return p.stdout

    def run(folder, exe, save, frames, script='0:.', extra=(), every=None):
        folder.mkdir(parents=True, exist_ok=True)
        cmd = [exe, '--os', '--mod', data/'nb', '--dataset', data, '--diskdir', data,
               '--loadstate', save, '--frames', frames, '--script', script,
               '--log', folder/'run.log', '--dumpram', folder/'final.ram',
               '--savestate-at', frames-1, folder/'final.sav', '--wav', folder/'audio.wav']
        if every: cmd += ['--dump', folder/'frame', '--dumpevery', every]
        cmd += list(extra)
        stdout = execute(cmd, folder)
        assert 'unmapped=0' in stdout, (folder, stdout)
        return folder

    def equal_outputs(a, b, pictures=True):
        for name in ('final.ram', 'final.sav', 'audio.wav'):
            assert (a/name).read_bytes() == (b/name).read_bytes(), (a, b, name)
        if pictures:
            files = sorted(a.glob('frame_*.ppm'))
            assert files
            for p in files: assert p.read_bytes() == (b/p.name).read_bytes(), (a, b, p.name)

    native_dir = out/'native'; native_dir.mkdir(exist_ok=True)
    if not args.resume_saves:
        print(execute([probe, '--native', evidence, '--log', native_dir/'run.log'], native_dir).strip(), flush=True)
        rows = json.loads((evidence/'search.json').read_text())
        save = evidence/'operator-snapshot/saves/Singleplayer/singleplayer.sav'
        initial = save.read_bytes()[RAM_OFFSET:RAM_OFFSET+RAM_SIZE]
        results = []
        for row in rows:
            folder = out/'natural'/row['name']; capture = folder/'boundaries'; capture.mkdir(parents=True, exist_ok=True)
            a = run(folder/'before', before, save, 3200, row['script'], every=50)
            b = run(folder/'after', after, save, 3200, row['script'], every=50)
            c = run(folder/'observer', probe, save, 3200, row['script'],
                    extra=['--death-captures', capture])
            equal_outputs(b, c, pictures=False)
            text = (c/'run.log').read_text()
            fixes = len(re.findall('^DEATH corrected ', text, re.M))
            end = (b/'final.ram').read_bytes()
            assert end[HERO+0x49] == initial[HERO+0x49]-1, (row['name'], 'life count')
            assert end[HERO+0x50:HERO+0x52] == initial[HERO+0x54:HERO+0x56], (row['name'], 'HP restore')
            deaths = re.findall(r'^DEATH subtract .*actor=2e7dc before=(\d+)', text, re.M)
            assert deaths == [str(initial[HERO+0x49])], (row['name'], deaths)
            if not fixes:
                equal_outputs(a, b)
            else:
                changed_at = int(re.search(r'DEATH corrected fr=(\d+)', text)[1])
                for p in a.glob('frame_*.ppm'):
                    if int(p.stem.rsplit('_', 1)[1]) <= changed_at:
                        assert p.read_bytes() == (b/p.name).read_bytes(), (row['name'], 'earlier picture')
                poses = re.findall(r'^DEATH end fr=(\d+) script=\w+ parts=(.*)', text, re.M)
                after_hit = [p for fr,p in poses if int(fr)>=changed_at]
                assert any('08192a00ffcc' in p for p in after_hit), 'fall missing'
                assert not any('08150701ffe4' in p for p in after_hit), 'kneeling returned'
            results.append(dict(name=row['name'], corrections=fixes, one_life=True, observer_identical=True,
                                unchanged=not fixes))
            print('PASS natural', row['name'], 'corrected' if fixes else 'byte-identical', flush=True)
        (out/'natural.json').write_text(json.dumps(results, indent=2))

        # Existing natural deaths, executions and paired kills: compare full
        # CPU/RAM save, PCM and regularly sampled rendered frames to the frozen
        # immediately-preceding engine, which already contains all earlier fixes.
        controls = [
            ('rat-paired', ROOT/'recomp/build/ko-fix-20260921/play/rat-fixed/input.sav', '0:.'),
            ('knight-ordinary', ROOT/'recomp/build/ko-fix-20260921/play/knight-fixed/input.sav',
             ','.join(f'{i}:lf,{i+25}:.' for i in range(0,250,50))),
            ('knight-decapitation', ROOT/'recomp/build/ko-fix-20260921/play/knight-alternate-fixed/input.sav', '0:.'),
            ('canopy-death', ROOT/'dist/MoonstoneNative/choke-stab-live-2026-08-09.sav', '0:.,180:df,300:.'),
            ('canopy-living-escape', ROOT/'dist/MoonstoneNative/choke-stab-live-2026-08-09.sav', '0:df,80:.'),
        ]
        control_results=[]
        for name, source, script in controls:
            a=run(out/name/'before', before, source, 600, script, every=5)
            b=run(out/name/'after', after, source, 600, script, every=5)
            equal_outputs(a,b)
            control_results.append(dict(name=name,frames=600,pictures=120,RAM_save_PCM_identical=True))
            print('PASS unchanged death/effect', name, flush=True)
        (out/'controls.json').write_text(json.dumps(control_results,indent=2))

    # Original states captured before/after selection, before/after the fix,
    # mid-fall and in the final corpse hold. Restore each from a fresh process
    # and from an already-running fight; no new host state or save version.
    boundaries=out/'natural/l-10/boundaries'
    saves=list(sorted(boundaries.glob('*.sav')))
    assert len(saves)==6, [p.name for p in saves]
    source=evidence/'operator-snapshot/saves/Singleplayer/singleplayer.sav'
    reports=[]
    for save in saves:
        a=run(out/'resumes'/save.stem/'cold',probe,save,500,every=10)
        b=run(out/'resumes'/save.stem/'warm',probe,source,600,every=10,
              extra=['--warm-fixture',save,'--warm-frame','100'])
        for name in ('final.ram','final.sav'):
            assert (a/name).read_bytes()==(b/name).read_bytes(), (save.stem,name)
        for p in a.glob('frame_*.ppm'):
            fr=int(p.stem.rsplit('_',1)[1]);assert p.read_bytes()==(b/f'frame_{fr+100:04d}.ppm').read_bytes(),(save.stem,fr)
        with wave.open(str(a/'audio.wav')) as x, wave.open(str(b/'audio.wav')) as y:
            y.setpos(100*882)
            xp=x.readframes(x.getnframes()); yp=y.readframes(y.getnframes())
            assert len(xp)==len(yp)
            # The output low-pass filter intentionally retains its warm audio
            # history. Its brief settling is outside the serialized Paula state;
            # require every subsequent sample to match, not merely similar RMS.
            differing=[i for i,(u,v) in enumerate(zip(xp,yp)) if u!=v]
            settling=1+max(differing)//4 if differing else 0
            assert settling<=64 and xp[256:]==yp[256:], (save.stem,'warm PCM',settling)
        ram=(a/'final.ram').read_bytes()
        original=source.read_bytes()[RAM_OFFSET:RAM_OFFSET+RAM_SIZE]
        assert ram[HERO+0x49]==original[HERO+0x49]-1
        reports.append(dict(boundary=save.stem,cold_warm_save_pixels_identical=True,
                            audio_filter_settling_samples=settling,one_life=True))
        print('PASS cold/warm save',save.stem,flush=True)
    (out/'saves.json').write_text(json.dumps(reports,indent=2))
    print('PASS: native, natural deaths, unchanged effects and save lifecycle',flush=True)


if __name__ == '__main__':
    main()
