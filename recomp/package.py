#!/usr/bin/env python3
"""Build and assemble a fresh public package. No development binaries or game data.

Never reuses/deletes a destination, reads a player save, or copies from dist.
Validation is mandatory before and after copying the allowlisted files.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

from release_gate import ARTIFACTS, validate_build, validate_package

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", nargs="?", type=Path,
                    default=HERE.parent / "dist/release-staging/Moonstone-2026-Windows-x64")
    args = ap.parse_args()
    out = args.output.resolve()
    if out.exists():
        ap.error(f"refusing to reuse existing staging folder: {out}")
    subprocess.run([sys.executable, str(HERE / "build.py"), "--profile", "release"], check=True)
    build = HERE / "build/release"
    validate_build(build)
    sources = {name: build / name for name in ARTIFACTS}
    sources.update({"controls.ini": HERE / "controls.ini", "README.txt": HERE / "dist_README.txt",
                    "RELEASE-NOTES.md": HERE.parent / "RELEASE-NOTES.md",
                    "LICENSE.txt": HERE.parent / "LICENSE",
                    "THIRD-PARTY-NOTICES.txt": HERE.parent / "THIRD-PARTY-NOTICES.txt",
                    "data/PLACE_ADF_FILES_HERE.txt": HERE / "PLACE_ADF_FILES_HERE.txt"})
    for source in sources.values():
        if not source.is_file():
            raise FileNotFoundError(source)
    out.mkdir(parents=True, exist_ok=False)
    for name in ("data", "saves/Singleplayer", "saves/Multiplayer"):
        (out / name).mkdir(parents=True)
    for name, source in sources.items():
        shutil.copy2(source, out / name)
    validate_package(out, build)
    print(f"Public release package verified: {out}")


if __name__ == "__main__":
    main()
