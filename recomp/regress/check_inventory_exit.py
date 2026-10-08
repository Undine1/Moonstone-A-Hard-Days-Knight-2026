#!/usr/bin/env python3
"""Compare native retail inventory exits and exercise X/B through actual SDL.

Uses copied controlled inventory fixtures, never writes an installed save/log.
The native oracle substitutes drawing/wait/audio leaves, not menu/cleanup code.
"""
from pathlib import Path
import argparse, json, shutil, struct, subprocess

ROOT = Path(__file__).resolve().parents[2]
RAM_START, RAM_SIZE = 104, 0x200000


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--modules', type=Path, required=True, help='port.ram/retail.ram originals')
    ap.add_argument('--fixtures', type=Path, required=True, help='Prepared stat-cap inventory fixtures')
    ap.add_argument('--probe', type=Path, default=ROOT/'recomp/build/inventory_exit_probe.exe')
    ap.add_argument('--native-only', action='store_true')
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    exe = out/'inventory_exit_probe.exe'
    shutil.copyfile(args.probe.resolve(), exe)
    shutil.copyfile(ROOT/'recomp/build/SDL2.dll', out/'SDL2.dll')
    data = ROOT/'dist/MoonstoneNative/data'
    reports = []

    def run(name, arguments):
        result = subprocess.run([str(exe), *map(str, arguments)], cwd=out,
                                capture_output=True, text=True, timeout=45,
                                creationflags=0x08000000)
        (out/(name+'.stdout.txt')).write_text(result.stdout+result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        line = next(s for s in result.stdout.splitlines() if s.startswith('PASS '))
        print(line, flush=True); reports.append(dict(case=name, result=line))

    run('native', ['--exit-oracle', args.modules.resolve(), '--log', out/'native.log'])
    if args.native_only:
        return

    get = lambda b, a, n=4: int.from_bytes(b[a:a+n], 'big')

    def case(scene, owner, keyboard, solo=False, variation=0):
        name = f's{scene}-p{owner+1}-{"key" if keyboard else "pad"}-solo{int(solo)}-v{variation}'
        folder = out/name; folder.mkdir(exist_ok=True)
        source = args.fixtures/f's{scene}-p{owner+1}-con-allowed-wait.sav'
        save = bytearray(source.read_bytes()); ram = save[RAM_START:RAM_START+RAM_SIZE]
        assert get(ram, 0x2fb1c) == scene
        assert get(ram, 0x2fb08) == 0x2e7dc+owner*0x84
        # Use all four human roster slots; only the current UI owner must claim.
        struct.pack_into('>H', save, RAM_START+0x2e024, 1 if solo else 4)
        for p in range(4):
            struct.pack_into('>I', save, RAM_START+0x2e7dc+p*0x84+0x36, p)
        table = get(ram, 0x3a96c)
        if variation == 5:
            for i in range(128):
                h = table+24*i
                assert get(ram, h+4, 2), 'No Exit hotspot'
                if get(ram, h+16) == 7:
                    struct.pack_into('>HH', save, RAM_START+0x392d4,
                                     get(ram, h+12, 2)+2, get(ram, h+14, 2)+2)
                    break
        prepared = folder/'input.sav'; prepared.write_bytes(save)
        # Missing action in an older INI must retain the new compiled defaults.
        config = '; Old config without the new action\n[keyboard]\ninventory = I\n'
        if variation in (3, 4):
            key, pad = ('C', 'X') if variation == 3 else ('none', 'none')
            config = f'[keyboard]\nclose_inventory = {key}\n[controller]\nclose_inventory = {pad}\n'
        (out/'controls.ini').write_text(config)
        command = ['--os', '--sdl', '--scale', '2', '--mod', data/'nb', '--dataset', data,
                   '--diskdir', data, '--loadstate', prepared, '--log', folder/'game.log',
                   '--exit-live', folder, '--owner', owner, '--variation', variation]
        if keyboard: command += ['--keyboard']
        if solo: command += ['--solo']
        run(name, command)

    for scene in (0, 3, 10):
        for owner in range(4):
            for keyboard in (False, True):
                case(scene, owner, keyboard)
    for keyboard in (False, True):
        case(0, 0, keyboard, solo=True)
        for variation in (2, 3, 4):
            case(0, 2, keyboard, variation=variation)
    for scene in (0, 3, 10):
        case(scene, 0, True, variation=5)
    (out/'results.json').write_text(json.dumps(reports, indent=2))
    print(f'PASS {len(reports)-1} SDL cases plus native oracle', flush=True)


if __name__ == '__main__':
    main()
