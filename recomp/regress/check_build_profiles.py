#!/usr/bin/env python3
"""Compare developer/public gameplay and reject developer-contaminated releases.

All outputs use a fresh scratch directory. Protected fixture files are read only.
Build build_profile_probe in both profiles first. --baseline-probe optionally
compares a frozen pre-change build of that same test observer as well.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

RECOMP = Path(__file__).resolve().parents[1]
ROOT = RECOMP.parent
sys.path.insert(0, str(RECOMP))
import release_gate as gate


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                            timeout=90, creationflags=0x08000000)
    assert not result.returncode, (command, result.stdout, result.stderr)
    return result


def rejected(call, why):
    try:
        call()
    except gate.ReleaseError as error:
        print(f"PASS rejected {why}: {error}", flush=True)
    else:
        raise AssertionError(f"Accepted {why}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--baseline-probe', type=Path)
    ap.add_argument('--package', type=Path)
    args = ap.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    build = RECOMP / 'build/release'
    gate.validate_build(build)
    if args.package: gate.validate_package(args.package, build)
    rejected(lambda: gate.validate_engine(RECOMP / 'build/moonstone.exe'), 'development binary')
    # Stronger than a fixed denylist: an unreviewed NEW CLI option also fails.
    mutant = out / 'new-debug-option.exe'
    mutant.write_bytes((build / 'moonstone.exe').read_bytes() + b'\x00--future-debug-tool\x00')
    rejected(lambda: gate.validate_engine(mutant), 'new developer switch')
    stale = out / 'stale'; stale.mkdir()
    record = json.loads((build / 'release-build.json').read_text())
    record['inputs']['src/moon.c'] = '0'*64
    (stale / 'release-build.json').write_text(json.dumps(record))
    rejected(lambda: gate.validate_build(stale), 'stale build provenance')
    swapped = out / 'swapped'; shutil.copytree(build, swapped)
    shutil.copyfile(RECOMP / 'build/moonstone.exe', swapped / 'moonstone.exe')
    rejected(lambda: gate.validate_build(swapped), 'swapped engine')
    if args.package:
        polluted = out / 'polluted-package'; shutil.copytree(args.package, polluted)
        (polluted / 'forgotten.pdb').write_bytes(b'test')
        rejected(lambda: gate.validate_package(polluted, build), 'extra file in package')
    # Actual public executable rejects every development-only option before play,
    # while --build-info/--version remain read-only (all runs carry scratch logs).
    source = (RECOMP / 'src/moon.c').read_text()
    options = set(re.findall(r'strcmp\(argv\[i\],"(--[a-z0-9-]+)"', source)) - gate.PUBLIC_OPTIONS
    for index, option in enumerate(sorted(options)):
        log = out / f'cli-{index}.log'
        result = subprocess.run([str(build / 'moonstone.exe'), '--log', str(log), option],
                                capture_output=True, text=True, timeout=10, creationflags=0x08000000)
        assert result.returncode == 2, (option, result.stdout, result.stderr)
        text = log.read_text()
        assert 'Unknown option' in text and 'SETUP RESULT' not in text
    print(f'PASS public executable rejects all {len(options)} developer CLI options', flush=True)
    for profile, folder in (('release',build),('development',RECOMP/'build')):
        text = run([folder/'moonstone.exe','--log',out/f'{profile}-identity.log','--build-info']).stdout
        assert text.strip() == f'MOONSTONE_BUILD_PROFILE:{profile}'
    data = ROOT / 'dist/MoonstoneNative/data'
    saves = ROOT / 'dist/MoonstoneNative'
    cases = [('combat','moonstone_combatrun.sav',600,'idle'),
             ('vanish','moonstone_bug.sav',600,'idle'),
             ('knight','moonstone_goodcombat.sav',600,'idle'),
             ('overland','moonstone_prebug_backup.sav',600,'idle'),
             ('town','moonstone_townmap.sav',800,'town'),
             ('armor','moonstone_townarmor.sav',2600,'armor'),
             ('campaign','moonstone_edgeknight.sav',600,'idle'),
             ('boot','-',500,'idle')]
    probes = [('development', RECOMP/'build/build_profile_probe.exe'),
              ('release', build/'build_profile_probe.exe')]
    if args.baseline_probe: probes.insert(0,('before',args.baseline_probe.resolve()))
    report = []
    for name, fixture, frames, mode in cases:
        reference = None
        for profile, exe in probes:
            folder=out/name/profile; folder.mkdir(parents=True)
            prefix=folder/'result'
            run([exe,data, '-' if fixture=='-' else saves/fixture,frames,mode,prefix,'--log',folder/'game.log'])
            result = [(prefix.with_suffix('.txt')).read_bytes(), (prefix.with_suffix('.sav')).read_bytes()]
            if reference is None: reference=result
            assert result == reference, f'{name}/{profile}: RAM, pixels, PCM, or full save changed'
        report.append(dict(case=name,frames=frames,result=reference[0].decode().strip()))
        print(f'PASS {name}: {frames} frames; RAM, pixels, PCM and complete save identical in {len(probes)} builds',flush=True)
    # Cross-profile save continuation, including observer sidecar reconstruction.
    reference=None
    for profile, exe in probes:
        prefix=out/f'cross-{profile}'
        run([exe,data,out/'combat/release/result.sav',200,'idle',prefix,'--log',out/f'cross-{profile}.log'])
        result=[prefix.with_suffix('.txt').read_bytes(),prefix.with_suffix('.sav').read_bytes()]
        if reference is None: reference=result
        assert reference==result, f'Cross-profile save continuation changed: {profile}'
    print('PASS cross-profile save continuation and atomic bad-load rejection',flush=True)
    (out/'results.json').write_text(json.dumps(dict(developer_options=len(options),cases=report,
        profiles=[p for p,_ in probes],cross_profile_saves=True),indent=2)+'\n')


if __name__=='__main__': main()
