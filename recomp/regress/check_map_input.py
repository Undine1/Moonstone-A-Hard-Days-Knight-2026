#!/usr/bin/env python3
"""Compare original retail map input and exercise SDL after a player is eliminated.

Requires the preserved map save and a separately built map_input_probe.exe.
All derived saves, logs and controls live under --output; original files stay intact.
"""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--retail', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'map_input_probe.exe'
    shutil.copyfile(ROOT / 'recomp/build/map_input_probe.exe', exe)
    shutil.copyfile(ROOT / 'recomp/build/SDL2.dll', out / 'SDL2.dll')
    shutil.copyfile(ROOT / 'recomp/controls.ini', out / 'controls.ini')
    data = ROOT / 'dist/MoonstoneNative/data'

    def run(name, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=45,
                                creationflags=0x08000000)
        (out / (name + '.txt')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (name, result.stdout[-3000:], result.stderr)
        print('\n'.join(line for line in result.stdout.splitlines() if line.startswith('PASS')), flush=True)

    run('native', [str(exe), '--loadstate', str(args.fixture.resolve()),
                   '--probe-retail', str(args.retail.resolve()), '--log', str(out / 'native.log')])
    original = args.fixture.read_bytes()
    for owner in range(4):
        # P3 uses the exact reported save. Other owners exercise the same map
        # loop with a dead preceding human, keeping the counter nonzero.
        save = bytearray(original)
        if owner != 2:
            def put(address, value, size=4):
                save[104 + address:104 + address + size] = value.to_bytes(size, 'big')
            actor = 0x2e7dc + owner * 0x84
            put(0x2e024, 4, 2)
            for p in range(4):
                put(0x2e7dc + p * 0x84 + 0x36, p)
                put(0x2e7dc + p * 0x84 + 0x49, 0 if p == (owner - 1) % 4 else 3, 1)
            put(0x2ebd0, actor)
            put(0x2e0bc, actor)
            put(0x2f9da, owner, 2)
            put(0x2fa02, 1, 2)
            struct.pack_into('<I', save, 20 + 9 * 4, actor)  # saved A1 in the redraw loop
        fixture = out / f'p{owner + 1}.sav'
        fixture.write_bytes(save)
        for keyboard in (False, True):
            for inventory in (False, True):
                for warm in (False, True):
                    name = f'p{owner + 1}-{"key" if keyboard else "pad"}-{"inventory" if inventory else "end"}-{"warm" if warm else "cold"}'
                    command = [str(exe), '--os', '--sdl', '--scale', '1',
                               '--mod', str(data / 'nb'), '--dataset', str(data),
                               '--diskdir', str(data), '--loadstate', str(fixture),
                               '--probe-owner', str(owner), '--log', str(out / (name + '.log'))]
                    if keyboard:
                        command += ['--probe-keyboard']
                    if inventory:
                        command += ['--probe-inventory']
                    if warm:
                        command += ['--probe-warm']
                    run(name, command)
    print('PASS 32 SDL cases: all four owners, shared controller/assigned keyboard, End Turn/inventory, cold/warm loads', flush=True)


if __name__ == '__main__':
    main()
