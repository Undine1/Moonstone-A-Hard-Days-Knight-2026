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
- **Swamp monster wedged inside the knight, flickering** — a point-blank pounce landed in a
  state the contact handler never handles (the standoff constant equals the re-pounce gate
  threshold), ping-ponging forever; it now resolves into the normal shake-it-off grab.

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

**A note on disk provenance:** the ADF images commonly circulating online are cracked
dumps that carry an **earlier, pre-release-ish build** of the game engine — not the boxed
retail release (which identifies itself as "v1.4"). The differences are not cosmetic:
the early build has genuine bugs Mindscape later fixed (broken shop transactions, combat
state leaks) and different game balance (shop prices, AI behaviour, damage rules).
Verified against SPS preservation images of the retail disks. The port detects the data
lineage at startup (see the `LINEAGE:` log line) and, on the cracked build, applies a
**retail-parity layer** (default on, `--noretailparity` to disable) that restores the
retail build's fixes and gameplay — see [CHANGES.md](CHANGES.md) § 1b. If you can dump
your own boxed disks (or use SPS/KryoFlux preservation images), prefer those.

**The famous "black knight suddenly has a Moonstone" bug**, seen across every copy of the
game for three decades, turned out to be two original defects stacked: the game's inventory
panels render a knight's raw weapon/armor **id fields with no range check**, and several id
values just past the legal range happen to index the three *Moonstone* name entries (and the
orb sprites) — so any knight whose equipment field holds a garbage id **displays as carrying
a Moonstone that does not exist** (ids further out send the hover-label renderer walking
garbage memory: the notorious inventory crash). That rendering flaw is in **both** builds,
including boxed retail. What made it epidemic on the circulating ADFs is that the earlier
build's broken shop code mass-produces exactly such garbage ids (an armor id written into
the weapon field); retail fixed the producer but kept the rendering flaw, so genuine-but-rare
sightings on original disks are expected too. The port fixes both ends: the parity layer
restores retail's shop code, and the renderer now validates ids (`--noinvmenufix` reverts).
No real Moonstone is involved: every actual token transfer in the game is player-sourced
(verified exhaustively — see CHANGES.md).

**The removed curse.** The original game contains a completely undocumented curse: an
overland hazard event drains the knight's hit points and marks him cursed, after which he
loses **one life at every day-end, indefinitely**. A cure exists and is just as
undocumented — a donation at the town healer lifts the curse (the healer even has a
dedicated dialog response for it). The mechanic appears in no manual and no period
walkthrough; its trigger behaves erratically (in our forensics it fired repeatedly once
all black knights were dead, and its selection logic has never been fully mapped even in
the code). This port **removes it**: it strikes players as random, and since neither the
cause nor the cure is discoverable, everyone who ever hit it experienced it as a
life-draining bug rather than a mechanic. The hazard body is skipped and any stale curse
flag in an existing save is cleared at day-end. Note this is distinct from Math the
Wizard's punishment for greedy repeat visitors — a few days of being too "ill" to win a
fight — which is time-limited, self-curing, and remains in the game.

## License

Copyright © 2026 Undine1. The port's runtime code in this repository is licensed under the
**GNU General Public License v3.0 (GPL-3.0)** — see [LICENSE](LICENSE). The original *Moonstone*
game code, data, and artwork are © 1991 Mindscape International / Rob Anderson and are **not**
distributed here.
