#!/usr/bin/env python3
"""Fail-closed checks for the public runtime and its exact package contents.

Uses the Python standard library; does not execute inspected executables.
The internal manifest is not a signature: it detects accidental stale builds,
swapped development/probe binaries, and changed source/toolchain inputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from pe_metadata import normalize
from engine_icons import validate_icons

HERE = Path(__file__).resolve().parent
ARTIFACTS = ("moonstone.exe", "Moonstone Singleplayer.exe", "Moonstone Multiplayer.exe", "SDL2.dll")
# Independently versioned, unchanged 1.4.2 launcher components. Rebuild from
# source, then require the exact tested binaries. Never bless an arbitrary old
# file or exempt the engine from its stricter public metadata policy.
STABLE_LAUNCHERS = {
    "Moonstone Singleplayer.exe": "4dc1cb997349f21b712079a14a43c0cae6ac504adc3400b2197e758eda48a2a7",
    "Moonstone Multiplayer.exe": "1e5e084f1cef421b562e283a9b02d8193976c504aded5b18fc453736a313cfe5",
}
PACKAGE_FILES = frozenset((*ARTIFACTS, "controls.ini", "README.txt", "RELEASE-NOTES.md",
                           "LICENSE.txt", "THIRD-PARTY-NOTICES.txt", "data/PLACE_ADF_FILES_HERE.txt"))
PACKAGE_DIRS = frozenset(("data", "saves", "saves/Singleplayer", "saves/Multiplayer"))
PUBLIC_OPTIONS = frozenset(("--log", "--save-profile", "--os", "--sdl", "--version", "--build-info",
                            "--scale", "--hardpan", "--audioblend", "--dataset", "--diskdir"))
FORBIDDEN_MARKERS = (
    b"MOONSTONE_BUILD_PROFILE:development", b"RECORDING FRAMES", b"capture-%d.wav",
    b"livedump", b"derail.sav", b"pcring(old->new)", b"AUDTRACE-", b"SCREECH-WATCH",
    b"INVLINK-SET", b"AIITEM-SET", b"ROT-WATCH", b"PENALTY-SET", b"MENULOOP ml=",
    b"WAVE-SPAWN", b"WAVE-DEATH", b"BEEP-OUT", b"SLOWFRAME", b"FRAMESTAT",
)


class ReleaseError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise ReleaseError(message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def input_hashes():
    paths = [HERE / name for name in ("build.py", "build.sh", "pe_metadata.py", "engine_icons.py", "release_gate.py", "package.py", "package.sh")]
    for folder in ("src", "vendor/Musashi-master", "vendor/SDL2"):
        paths += [p for p in (HERE / folder).rglob("*")
                  if p.is_file() and p.suffix.lower() in (".c", ".h", ".a", ".dll")]
    paths += list((HERE / "icon").glob("*.rc")) + list((HERE / "icon").glob("*.ico"))
    paths.append(HERE / "tools/zig-x86_64-windows-0.16.0/zig.exe")
    return {p.relative_to(HERE).as_posix(): sha256(p) for p in sorted(set(paths))}


def inspect_pe(path, *, stable_launcher=False):
    data = Path(path).read_bytes()
    if stable_launcher:
        require(sha256(path) == STABLE_LAUNCHERS.get(Path(path).name),
                f"{path}: launcher component changed; review and test before releasing")
        # Validate that the preserved debug directory is only an empty REPRO
        # record, not a PDB, symbol payload or developer instrumentation.
        normalize(data)
    require(data[:2] == b"MZ", f"{path}: not a PE executable")
    try:
        pe, = struct.unpack_from("<I", data, 0x3c)
        require(data[pe:pe+4] == b"PE\0\0", f"{path}: bad PE signature")
        machine, count, timestamp, symptr, symbols, optsize, _ = struct.unpack_from("<HHIIIHH", data, pe+4)
        require(machine == 0x8664, f"{path}: expected x64")
        require(timestamp == 0 or stable_launcher, f"{path}: public PE timestamp must be unspecified")
        opt = pe+24
        require(struct.unpack_from("<H", data, opt)[0] == 0x20b, f"{path}: expected PE32+")
        require(struct.unpack_from("<H", data, opt+68)[0] == 2, f"{path}: expected GUI subsystem")
        require(not symptr and not symbols, f"{path}: COFF debug symbols included")
        for i in range(count):
            start = opt + optsize + i*40
            name, size, rva, rawsize, raw = struct.unpack_from("<8sIIII", data, start)
            name = name.rstrip(b"\0")
            require(not name.startswith((b".debug", b"/")), f"{path}: debug section {name!r}")
            require(name != b".buildid" or stable_launcher, f"{path}: unused linker build marker included")

        drva, dsize = struct.unpack_from("<II", data, opt+112+6*8)
        require((not drva and not dsize) or stable_launcher, f"{path}: public debug directory included")
        require(not re.search(rb"(?i)\.pdb\x00", data), f"{path}: PDB reference included")
        return {"size": len(data), "debug_types": [16] if stable_launcher else [], "sha256": sha256(path)}
    except (struct.error, IndexError) as error:
        raise ReleaseError(f"{path}: truncated PE") from error


def validate_engine(path):
    result = inspect_pe(path)
    validate_icons(path)
    data = Path(path).read_bytes()
    require(b"MOONSTONE_BUILD_PROFILE:release\0" in data, f"{path}: release profile marker missing")
    for marker in FORBIDDEN_MARKERS:
        require(marker not in data, f"{path}: developer feature present: {marker.decode()}")
    strings = re.findall(rb"[ -~]{3,}\x00", data)
    options = {x.decode() for s in strings for x in
               re.findall(rb"(?<![a-zA-Z0-9-])--[a-z][a-z0-9-]*(?=[\s\x00])", s)}
    require(not options - PUBLIC_OPTIONS, f"{path}: nonpublic switches: {sorted(options - PUBLIC_OPTIONS)}")
    return result


def validate_build(folder):
    folder = Path(folder)
    try:
        record = json.loads((folder / "release-build.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseError("Missing/invalid release manifest; rebuild with build.py --profile release") from error
    require(record.get("schema") == 1 and record.get("profile") == "release", "Wrong build profile")
    require(record.get("inputs") == input_hashes(), "Release inputs changed; rebuild before packaging")
    require(set(record.get("artifacts", {})) == set(ARTIFACTS), "Incomplete release artifact manifest")
    results = {}
    for name in ARTIFACTS:
        path = folder / name
        require(path.is_file() and not path.is_symlink(), f"Missing/linked release artifact: {name}")
        require(sha256(path) == record["artifacts"][name], f"Release artifact changed: {name}")
        if name.endswith(".exe"):
            results[name] = validate_engine(path) if name == "moonstone.exe" else inspect_pe(path, stable_launcher=True)
    return results


def validate_package(folder, build):
    folder, build = Path(folder), Path(build)
    results = validate_build(build)
    entries = list(folder.rglob("*"))
    require(not any(p.is_symlink() for p in entries), "Package must not contain symbolic links")
    files = {p.relative_to(folder).as_posix() for p in entries if p.is_file()}
    dirs = {p.relative_to(folder).as_posix() for p in entries if p.is_dir()}
    require(files == PACKAGE_FILES, f"Unexpected/missing package files: {sorted(files ^ PACKAGE_FILES)}")
    require(dirs == PACKAGE_DIRS, f"Unexpected/missing package directories: {sorted(dirs ^ PACKAGE_DIRS)}")
    for name in ARTIFACTS:
        require(sha256(folder / name) == sha256(build / name), f"Package artifact mismatch: {name}")
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", type=Path, default=HERE / "build/release")
    ap.add_argument("--package", type=Path)
    args = ap.parse_args()
    result = validate_package(args.package, args.build) if args.package else validate_build(args.build)
    print(json.dumps(result, indent=2))
    print("Release checks passed")


if __name__ == "__main__":
    main()
