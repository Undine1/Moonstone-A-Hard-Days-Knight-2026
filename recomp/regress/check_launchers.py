"""Isolated Windows launcher tests; no game, personal saves or logs are opened.

Uses the existing Zig compiler for test-only observers. --previous optionally
compares the real old/new launchers with the same child, paths and raw input.
Requires a fresh --output directory. Never distribute the observer EXEs.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
RECOMP = ROOT / 'recomp'
PROFILES = ('Singleplayer', 'Multiplayer')
FLAGS = 0x08000000


def record(path):
    data = path.read_bytes()
    magic, argc, console = struct.unpack_from('<III', data)
    assert magic == 0x4c41554e
    pos, texts = 12, []
    for _ in range(argc + 3):
        size, = struct.unpack_from('<I', data, pos)
        pos += 4
        texts.append(data[pos:pos + size * 2].decode('utf-16-le'))
        pos += size * 2
    assert pos == len(data)
    return dict(raw=texts[0], cwd=texts[1], marker=texts[2], argv=texts[3:], console=console)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--launchers', type=Path, default=RECOMP / 'build')
    ap.add_argument('--previous', type=Path)
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    assert os.name == 'nt'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    unrelated = out / 'Unrelated working directory'
    unrelated.mkdir()
    build_env = dict(os.environ, ZIG_LOCAL_CACHE_DIR=str(out / 'zig-local-cache'),
                     ZIG_GLOBAL_CACHE_DIR=os.environ.get('ZIG_GLOBAL_CACHE_DIR', str(out / 'zig-global-cache')))
    compiler = RECOMP / 'tools/zig-x86_64-windows-0.16.0/zig.exe'
    builds = [('child.exe', ['-DLAUNCHER_CHILD_PROBE', '-Wl,--subsystem,windows'])]
    builds += [(f'unit-{p}.exe', [f'-DMOON_MULTIPLAYER={i}']) for i, p in enumerate(PROFILES)]
    for name, flags in builds:
        result = subprocess.run([str(compiler), 'cc', '-O2', '-std=c11', '-municode',
            '-Wall', '-Wextra', '-Werror', *flags, 'regress/launcher_probe.c', '-luser32',
            '-o', str(out / name)], cwd=RECOMP, env=build_env, capture_output=True,
            text=True, timeout=120, creationflags=FLAGS)
        (out / (name + '-build.txt')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
    missing, invalid = out / 'Missing child', out / 'Invalid child'
    missing.mkdir(); invalid.mkdir()
    (invalid / 'moonstone.exe').write_text('This is deliberately not an executable.')
    units = []
    for profile in PROFILES:
        try:
            result = subprocess.run([out / f'unit-{profile}.exe', missing, invalid],
                cwd=unrelated, capture_output=True, text=True, timeout=15, creationflags=FLAGS)
        except subprocess.TimeoutExpired as error:
            raise AssertionError((profile, error.stdout, error.stderr)) from error
        assert result.returncode == 0, result.stdout + result.stderr
        units.append(result.stdout.strip())
        print(units[-1], flush=True)

    # A moved installation, Unicode/surrogate pairs, shell metacharacters, and
    # a path near MAX_PATH. All paths remain inside this new scratch directory.
    staged = out / 'Initially assembled'
    staged.mkdir()
    moved = out / 'Moved Game with spaces'
    staged.rename(moved)
    installs = [moved, out / 'Unicode Å 雪 \U0001f319 & (game)',
                out / ('Near limit ' + 'g' * 80)]
    outcomes = []
    sources = [('new', args.launchers.resolve())]
    if args.previous:
        sources.insert(0, ('old', args.previous.resolve()))
    for install in installs:
        install.mkdir(exist_ok=True)
        shutil.copyfile(out / 'child.exe', install / 'moonstone.exe')
        for profile in PROFILES:
            launcher = install / f'Moonstone {profile}.exe'
            log = out / f'{len(outcomes)} scratch log.txt'
            basic = subprocess.list2cmdline(['--log', str(log)])
            quoted = subprocess.list2cmdline(['--log', str(log), '', 'two words',
                'quote"inside', 'C:\\trailing path\\', 'Å雪\U0001f319', '%PATH%', '& | > ^'])
            # The raw tail must be forwarded verbatim, including whitespace,
            # empty values, backslashes, quotes and non-ASCII characters.
            cases = [('', ''), ('\t  ', ''), (' ', basic), ('\t', quoted),
                     (' ', basic + '   --literal "a\\\\\\\"b"  '),
                     ('\r\n ', basic + ' --long ' + 'x' * 8000)]
            for case_number, (separator, tail) in enumerate(cases):
                pair = {}
                for label, source in sources:
                    shutil.copyfile(source / launcher.name, launcher)
                    result_path = out / f'child-{len(outcomes)}-{case_number}-{label}.bin'
                    child_env = dict(os.environ, MOON_LAUNCHER_PROBE_RESULT=str(result_path),
                                     MOON_LAUNCHER_PROBE_MARKER='environment inherited')
                    # Use explicit lpApplicationName and a raw command, as a
                    # Windows shortcut does; no shell interprets these strings.
                    command = f'"{launcher}"{separator}{tail}'
                    done = subprocess.run(command, executable=str(launcher), cwd=unrelated,
                        env=child_env, capture_output=True, timeout=15, creationflags=FLAGS)
                    assert done.returncode == 0, (label, done.returncode, done.stderr)
                    deadline = time.monotonic() + 10
                    got = None
                    while time.monotonic() < deadline:
                        try:
                            got = record(result_path)
                            break
                        except (FileNotFoundError, PermissionError):
                            # The child publishes a complete record by rename.
                            # Windows can briefly deny access after publication;
                            # retry that sharing race within the same deadline.
                            time.sleep(0.02)
                    assert got is not None, (label, install, case_number)
                    expected = f'"{install / "moonstone.exe"}" --os --sdl --save-profile {profile.lower()} {tail}'
                    assert got['raw'] == expected, (got['raw'], expected)
                    assert Path(got['cwd']) == install
                    assert got['marker'] == 'environment inherited' and not got['console']
                    assert got['argv'][:5] == [str(install / 'moonstone.exe'), '--os', '--sdl',
                                               '--save-profile', profile.lower()]
                    if case_number in (2, 3):
                        # Independently verify parsed values, not just A/B raw equality.
                        expected_args = ['--log', str(log)]
                        if case_number == 3:
                            expected_args += ['', 'two words', 'quote"inside', 'C:\\trailing path\\',
                                              'Å雪\U0001f319', '%PATH%', '& | > ^']
                        assert got['argv'][5:] == expected_args
                    pair[label] = got
                if 'old' in pair:
                    assert pair['old'] == pair['new'], (install, profile, case_number)
                outcomes.append(dict(folder=install.name, profile=profile, case=case_number,
                                     compared_previous='old' in pair))
            print(f'PASS {profile}: {ascii(install.name)} (6 real-process cases)', flush=True)
    assert not list(out.rglob('*.sav'))
    assert not list(unrelated.iterdir())
    (out / 'results.json').write_text(json.dumps(dict(units=units, launches=outcomes), indent=2))
    print(f'PASS {len(outcomes)} launcher cases; both profiles; no saves created', flush=True)


if __name__ == '__main__':
    main()
