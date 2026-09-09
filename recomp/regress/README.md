# Regression check

A tripwire so fixes that quietly come undone are caught here in seconds, not mid-playthrough.

```sh
bash recomp/build.sh                 # build the engine
python recomp/regress/check.py       # run all goldens -> PASS / FAIL
```

Exit code is non-zero if anything regressed (usable in CI / a pre-push hook).

Console-free startup/dialog checks and the audit of former console users are
documented in [console-audit.md](console-audit.md).

`python recomp/regress/check_data_setup.py` checks first-run disk extraction,
missing and invalid disks/modules, filesystem and file-chain diagnostics, read
denials, and actual folder write denials in isolated scratch directories.
Pass `--host-probe-exe recomp/build/host_error_probe.exe` to also verify partial
writes and close failures. The Windows permission tests restore their temporary
directory ACLs before cleanup; source disks and installed files are read only.

`python recomp/regress/check_choke_death.py` replays the frozen canopy choke
through player death, attempts to stab after death, and save/reload. It also
checks that living escape behavior and death-to-inventory timing are unchanged.
Pass `--output recomp/build/choke-review` to retain frame dumps for visual review.

`python recomp/regress/check_practice_input.py` boots into Practice and checks
movement, attack, released controls, damage, and save/reload for the second human
knight. Disabled-fix comparisons must reproduce the former input leakage.

## What it checks

- **`ramfnv`** — cold-boots the engine to a fixed frame and FNV-1a hashes all of RAM.
  Catches *any* unintended change to engine/game behaviour (a stray write, a logic change).
- **`framesha`** — loads a save, renders a fixed frame, SHA-256s the pixels. Catches
  *display* regressions (e.g. an outro/ending screen rendering garbled).

Goldens live in [`manifest.tsv`](manifest.tsv) — only the **hashes** are committed. The
saves and `.adf` game data they reference are local copyrighted content and stay out of
the repo. The engine log is redirected to a temp dir, so your `moonstone.log` is untouched.

## Adding a golden

1. Get the state into a save (e.g. F5 the win screen) under `dist/MoonstoneNative/`.
2. Add a `framesha` row with any placeholder golden.
3. **Confirm the state renders correctly**, then baseline it:
   `python recomp/regress/check.py --update`

## After an intentional change

A real behaviour/render change will (correctly) FAIL. Eyeball that the new output is what
you wanted, then re-baseline: `python recomp/regress/check.py --update`.
