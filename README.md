# Moonstone Reborn

Native Windows port of *Moonstone: A Hard Days Knight* (Mindscape, 1991), running the
original Amiga 68000 game code on a **from-scratch model of the Amiga's custom chips** —
no emulator, no Kickstart ROM. An embedded 68000 core executes the real game code on top
of a hand-written OCS chipset (blitter, copper, 4-channel Paula audio, trackdisk).

See [CHANGES.md](CHANGES.md) for everything this port changes vs. the original.

## Original-game bugs fixed

The port also corrects several defects present in the 1991 release itself, among them:

- **The Moonstone-in-enemy crash** — a black knight looting the Moonstone corrupted the
  inventory list and crashed the game on the next click.
- **The two-troll / double-overhead-swing crash** — simultaneous screen-effect starts leaked
  per-vblank task-list entries until the list overflowed (also froze the post-combat cursor).
- **AI knights walking into the map edge** — the overland AI's fallback goal had no arrival
  check, so a knight would overshoot his destination and grind against the map border all turn.
- **AI knights never attacking on contact** — a knight with town business (the AI's gold-driven
  shopping/healing intent) had his per-tick attack check masked for the whole chase, so he'd
  march straight onto the player — or right through him — without ever engaging.
- **Town exit not ending the day after an armor purchase** — the exit stamps the turn timer
  to the *old* movement budget, then the new armor's bigger budget is re-derived on the map
  return, resurrecting the turn.
- **Player vanishing mid-combat** — the grab/pounce choreography *toggles* the knight's
  sprite-hide flag rather than setting it, so one unmatched toggle left him invisible,
  uncontrollable and the fight unwinnable; hide/show intent is now explicit.

Most of these ship with an off-switch restoring the faithful-original behaviour
(`--notaskfix`, `--noedgewalkfix`, …) — the full list and details are in
[CHANGES.md](CHANGES.md) §1.

## Layout

| Path | What |
|------|------|
| `recomp/src/moon.c` | The engine: chipset model + host harness (SDL2 window/input/audio). |
| `recomp/src/loader.c` | Module loader + AmigaOS exec/dos HLE (no Kickstart). |
| `recomp/src/*.h` | Generated assets (app icon, boot splash) the build `#include`s. |
| `recomp/build.sh` | Build with `zig cc` (no MSVC). |
| `recomp/icon/` | Application-icon + boot-splash source (`.rc`, `.ico`, source PNGs). |
| `recomp/RE_NOTES.md` | Reverse-engineering notes. |
| `recomp/tools/`, `recomp/*.py` | RE / audio / image tooling. |

## Building

Third-party dependencies are **not** committed (downloaded, not ours to version):

- **zig 0.16.0** → `recomp/tools/zig-x86_64-windows-0.16.0/`
- **SDL2 2.32.10** (mingw dev) → `recomp/vendor/SDL2/`
- **Musashi** 68000 core → `recomp/vendor/Musashi-master/`

Then:

```sh
bash recomp/build.sh        # -> recomp/build/moonstone.exe (+ SDL2.dll)
```

## Game data

The original Mindscape game disks and modules are **not** included (copyright). To run,
supply your own `Moonstone ... Disk1/2/3.adf`; the runtime extracts the boot modules from
them automatically.

## License

Copyright © 2026 Undine1. The port's runtime code in this repository is licensed under the
**GNU General Public License v3.0 (GPL-3.0)** — see [LICENSE](LICENSE). The original *Moonstone*
game code, data, and artwork are © 1991 Mindscape International / Rob Anderson and are **not**
distributed here.
