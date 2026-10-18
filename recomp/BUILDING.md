# Building and packaging

Use the bundled Zig/SDL toolchain. Do not install or update dependencies without
the operator's approval. From the repository root:

```powershell
python recomp/build.py
python recomp/regress/check.py --exe C:/absolute/path/to/recomp/build/moonstone.exe
python recomp/package.py C:/absolute/path/to/a/NEW/Moonstone-2026-Windows-x64
```

`build.sh` and `package.sh` call these same Python recipes when Bash is available.
There is one maintained source/build recipe, with two compilation profiles:

| Profile | Engine | Developer tools |
| --- | --- | --- |
| Development (default build) | `recomp/build/moonstone.exe` | F10/F12 recording, scripts, traces, memory/disassembly tools and regression options |
| Release (`--profile release`) | `recomp/build/release/moonstone.exe` | Compiled out; no developer symbols/PDB references |

Both retain normal error logging, OS/architecture and startup diagnostics,
F5/F9 save/load, F11 fullscreen, controllers, all gameplay fixes and save format5.
The two save launchers are the same stripped Windows-API binaries in either profile.
They retain their independent 1.4.2 component version and original linker identity
while their code is unchanged; this does not change the game's v1.5 version or
save slots. Every build reproduces them from source and requires the exact tested
hashes. A launcher change requires a separate review/test/update of those hashes.
The public engine leaves the Windows PE timestamp unspecified (zero) and omits the
linker's empty reproducibility section. The build checks this metadata cleanup
preserves code/data/resource bytes and loaded addresses; the package gate rejects
timestamps or build-marker sections returning to the engine. Only the two exact
approved launcher hashes may retain their empty REPRO markers. Development
engine symbols and normal startup/error diagnostics remain available.
No run-time switch can enable developer tools in a release. Direct compiler builds
default to release; explicitly define `MOON_DEVELOPER=1` for developer probes.

Release engines store the original six Windows icon sizes as uncompressed
32-bit bitmaps. `engine_icons.py` applies and verifies this resource-only step;
code, game data, runtime addresses and icon pixels stay unchanged. The source
`icon/moonstone.ico` remains untouched for the stable launchers and development
engine. Normal builds use the checked-in `icon/moonstone-raw.ico` with no new
dependencies. If the artwork intentionally changes, regenerate that asset with
`python recomp/tools/icon_to_raw.py recomp/icon/moonstone.ico recomp/icon/moonstone-raw.ico`
and run `regress/check_engine_icons.py` before packaging. The gate rejects the
old compressed resources returning to a public engine.

**Never package by copying `recomp/build/moonstone.exe` or the installed dev folder.**
`package.py` always builds release, then checks source/toolchain/artifact hashes,
the compiled profile, developer CLI strings, PE debug data, and an exact file
allowlist. It refuses existing destinations and validates again after copying.
Only those validated runtime files and empty save folders belong in the public ZIP.
The internal `release-build.json`, `.pdb` files, test EXEs, logs, recordings,
ADFs and extracted game modules must never be distributed.

Run the gate once more before uploading a prepared folder:

```powershell
python recomp/release_gate.py --package C:/absolute/path/to/Moonstone-2026-Windows-x64
```

A changed input or binary invalidates the manifest; rebuild through the package
command into a fresh folder. Do not bypass a failed check. The manifest catches
packaging mistakes; it is not an authenticity signature or antivirus guarantee.

Developer tests can use the same recipe without creating a release manifest:

```powershell
python recomp/build.py build_profile_probe --probe recomp/regress/build_profile_probe.c
python recomp/build.py build_profile_probe --probe recomp/regress/build_profile_probe.c --profile release
python recomp/build.py --typecheck
python recomp/build.py --typecheck --profile release
```

Never deploy a probe executable. See `regress/README.md` for profile, save,
fullscreen, controller and startup tests. Every game/launcher test must pass
an absolute scratch `--log`; a bare run truncates the player's log.

The boot splash deliberately embeds raw RGB24 pixels, without base64, image
compression or a runtime decoder. Regenerate it with
`python recomp/tools/splash_to_h.py recomp/icon/splash.png`; keep the source image and
960x768 output unchanged unless an artwork change is requested. The compiler
checks the array length, and `check_splash.py` verifies its pixels against the
source and an earlier build. Normal builds consume this checked-in header.
