"""Guardian reward/map redraw: compare game state/audio and exact presented frames.

Requires a frozen preceding development executable and a reward-wait fixture.
All generated saves, pictures and logs are private regression evidence.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'fixture', 'data', 'output'):
        ap.add_argument('--'+name, type=Path, required=True)
    ap.add_argument('--control', action='append', type=Path, default=[])
    args = ap.parse_args()
    before, fixture, data, out = (getattr(args, x).resolve()
                                  for x in ('before', 'fixture', 'data', 'output'))
    after = ROOT/'recomp/build/moonstone.exe'
    assert before.is_file() and before.with_name('SDL2.dll').is_file()
    out.mkdir(parents=True, exist_ok=True)
    results = []

    def run(name, exe, save, script, raw=False, frames=300, every=1):
        d = out/name; d.mkdir(exist_ok=True)
        command = [exe, '--os', '--mod', data/'nb', '--dataset', data,
                   '--diskdir', data, '--loadstate', save, '--frames', frames,
                   '--script', script, '--log', d/'run.log', '--dump', d/'frame',
                   '--dumpevery', every, '--dumpram', d/'final.ram',
                   '--savestate-at', frames-1, d/'final.sav', '--wav', d/'audio.wav']
        if raw:
            command.append('--rawcapture')
        p = subprocess.run(list(map(str, command)), cwd=d, capture_output=True,
                           text=True, timeout=90, creationflags=0x08000000)
        (d/'stdout.txt').write_text(p.stdout+p.stderr)
        assert p.returncode == 0 and 'stop=?' in p.stdout, (name, p.stdout, p.stderr)
        return d

    def compare(name, save, script, transition, raw=False, every=1):
        a = run(name+'-before', before, save, script, raw, every=every)
        b = run(name+'-after', after, save, script, raw, every=every)
        for filename in ('final.ram', 'final.sav', 'audio.wav'):
            assert (a/filename).read_bytes() == (b/filename).read_bytes(), (name, filename)
        changed = []
        log = (b/'run.log').read_text()
        start = re.findall(r'GUARDIAN-RETURN map-redraw fr=(\d+)', log)
        end = re.findall(r'GUARDIAN-RETURN fade-complete fr=(\d+)', log)
        if transition:
            assert len(start) == len(end) == 1, name
            start, end = int(start[0]), int(end[0])
        pictures = sorted(b.glob('frame_*.ppm'))
        assert pictures
        for p in pictures:
            frame = int(p.stem.split('_')[-1])
            new = p.read_bytes()
            if new != (a/p.name).read_bytes():
                assert transition and not raw and start <= frame < end, (name, frame)
                assert not any(new.split(b'\n', 3)[3]), (name, frame, 'not black')
                changed.append(frame)
        if transition and not raw:
            assert changed, (name, 'original flash not exercised')
        results.append(dict(name=name, changed_frames=changed, state_audio_equal=True))
        print('PASS', name, 'state/save/audio identical; changed frames', changed, flush=True)

    for down, up in ((1, 4), (10, 18), (31, 40), (50, 62), (113, 121)):
        compare('reward-'+str(down), fixture, f'{down}:f,{up}:.', True)
    compare('raw', fixture, '10:f,18:.', True, raw=True)
    compare('waiting', fixture, '0:.', False, every=10)
    for i, path in enumerate(args.control):
        compare('control-'+str(i), path.resolve(), '0:.', False, every=10)
    (out/'results.json').write_text(json.dumps(results, indent=2)+'\n')


if __name__ == '__main__':
    main()
