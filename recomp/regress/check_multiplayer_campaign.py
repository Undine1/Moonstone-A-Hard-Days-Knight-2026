#!/usr/bin/env python3
"""Exercise original campaign selection/turns with actual SDL virtual controllers.

All installs, saves, settings and logs are scratch. Local ADF data is read-only.
Build multiplayer_probe.c first, as documented in regress/README.md.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--menu', type=Path, required=True)
    ap.add_argument('--case', default='2:1,2:2,3:3,4:4,4:2,4:1',
                    help='players:pads[:keyboard-player], comma separated; keyboard player is 1-based')
    ap.add_argument('--duel-fixture', type=Path)
    ap.add_argument('--duel-after-setup', action='store_true',
                    help='Test both duel roles after actual device choices, names and turns')
    ap.add_argument('--duel-opponent',type=int,help='1-based opponent of P1; defaults to last player')
    ap.add_argument('--duel-keyboard',action='store_true',
                    help='P1/P2 initially share a pad; explicitly borrow unused keyboard for their duel')
    ap.add_argument('--restore-context', type=int)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'dist/MoonstoneNative/data'
    with tempfile.TemporaryDirectory(prefix='moon-campaign-') as temp:
        install = Path(temp)
        exe = install / 'multiplayer_probe.exe'
        shutil.copyfile(ROOT / 'recomp/build/multiplayer_probe.exe', exe)
        shutil.copyfile(ROOT / 'recomp/build/SDL2.dll', install / 'SDL2.dll')
        shutil.copyfile(ROOT / 'recomp/controls.ini', install / 'controls.ini')
        for case in args.case.split(','):
            parts = case.split(':')
            players, pads = parts[:2]
            keyboard_player = parts[2] if len(parts) == 3 else '0'
            name = f'campaign-{players}p-{pads}pads'
            if keyboard_player != '0':
                name += f'-kb{keyboard_player}'
            command = [str(exe), '--os', '--sdl', '--scale', '2',
                       '--mod', str(data / 'nb'), '--dataset', str(data), '--diskdir', str(data),
                       '--loadstate', str((args.duel_fixture or args.menu).resolve())]
            for reverse in ((0, 1) if (args.duel_fixture or args.duel_after_setup) and args.restore_context is None else (None,)):
                run_name = name if reverse is None else f'{name}-duel-{reverse}'
                mode = ['--probe-campaign-setup', players, pads] if reverse is None else \
                       ['--probe-campaign-duel', players, pads, str(reverse)]
                if args.restore_context is not None:
                    mode = ['--probe-campaign-restore', players, pads, str(args.restore_context)]
                if args.duel_after_setup:
                    mode = ['--probe-campaign-setup', players, pads, '--probe-duel-after-setup', str(reverse)]
                if args.duel_opponent is not None:
                    assert 2<=args.duel_opponent<=int(players)
                    mode += ['--probe-duel-opponent',str(args.duel_opponent)]
                if args.duel_keyboard:
                    assert args.duel_after_setup and args.duel_opponent==2 and keyboard_player=='0'
                    mode += ['--probe-duel-keyboard']
                result = subprocess.run(command + mode + [
                    '--probe-keyboard-player', keyboard_player,
                    '--probe-campaign-save', str(out / (run_name + '.sav')),
                    '--probe-menu-fixture', str(args.menu.resolve()),
                    '--probe-image', str(out / run_name), '--log', str(out / (run_name + '.log'))],
                    capture_output=True, text=True, timeout=105)
                assert result.returncode == 0, f'{run_name}: {result.stdout}\n{result.stderr}'
                print(result.stdout.strip(), flush=True)


if __name__ == '__main__':
    main()
