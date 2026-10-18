#!/usr/bin/env python3
"""Exercise release gates for the independently versioned save launchers.

All mutations are private test copies in a new output directory. No game runs.
"""
import argparse
import json
from pathlib import Path
import sys

RECOMP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RECOMP))
from pe_metadata import normalize
from release_gate import ReleaseError, STABLE_LAUNCHERS, inspect_pe, validate_build, validate_engine


def rejected(call, label):
    try:
        call()
    except ReleaseError:
        print(f'PASS rejected {label}')
    else:
        raise AssertionError(f'Accepted {label}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=RECOMP / 'build/release')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    validate_build(args.build)
    for name in STABLE_LAUNCHERS:
        source = args.build / name
        result = inspect_pe(source, stable_launcher=True)
        assert result['debug_types'] == [16]
        rejected(lambda: inspect_pe(source), f'launcher treated as normalized engine: {name}')
        rejected(lambda: validate_engine(source), f'launcher used as game: {name}')
        for label, data in (
            ('changed byte', source.read_bytes()[:-1] + bytes([source.read_bytes()[-1] ^ 1])),
            ('normalized component', normalize(source.read_bytes())),
            ('other profile', (args.build / next(n for n in STABLE_LAUNCHERS if n != name)).read_bytes()),
            ('appended debug switch', source.read_bytes() + b'\0--test-probe\0'),
        ):
            dest = args.output / label / name
            dest.parent.mkdir(exist_ok=True)
            dest.write_bytes(data)
            rejected(lambda: inspect_pe(dest, stable_launcher=True), f'{label}: {name}')
    rejected(lambda: inspect_pe(args.build / 'moonstone.exe', stable_launcher=True), 'engine under launcher exception')
    rejected(lambda: validate_engine(RECOMP / 'build/moonstone.exe'), 'development engine')
    stale = args.output / 'stale'
    stale.mkdir()
    record = json.loads((args.build / 'release-build.json').read_text())
    record['inputs']['src/save_launcher.c'] = '0' * 64
    (stale / 'release-build.json').write_text(json.dumps(record))
    rejected(lambda: validate_build(stale), 'stale launcher source provenance')
    print('PASS stable components accepted; 15 contamination/provenance checks rejected')


if __name__ == '__main__':
    main()
