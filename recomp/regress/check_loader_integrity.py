#!/usr/bin/env python3
"""Exercise stream boundaries and changed startup data using private fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe-exe', type=Path, default=ROOT / 'recomp/build/loader_integrity_probe.exe')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='moonstone-loader-integrity-') as temp:
        folder = Path(temp)
        result = subprocess.run([str(args.probe_exe.resolve()), '--probe-stream-tests', str(folder),
                                 '--log', str(folder / 'stream.log')], cwd=folder,
                                capture_output=True, text=True, timeout=15,
                                creationflags=0x08000000 if os.name == 'nt' else 0)
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'PASS 21 stream bounds and saved-stream validation cases' in result.stdout
        print(result.stdout.strip())


if __name__ == '__main__':
    main()
