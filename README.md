# Moonstone Reborn

Native Windows port of *Moonstone: A Hard Days Knight* (Mindscape, 1991). An
embedded 68000 core runs the original Amiga game code on a from-scratch model of
the OCS custom chips, with no external emulator or Kickstart ROM required.

## Highlights

- Portable native Windows executable with automatic disk swapping.
- Keyboard, mouse, and game-controller support.
- Quicksave and quickload anywhere, including during combat.
- Faithful graphics and Paula audio through the custom OCS runtime.
- Retail-parity support for the commonly circulating cracked ADF revision.

## Confirmed original Amiga bugs fixed

Only bugs corroborated by Amiga players before this port are labelled as
original-game bugs here:

- **Two trolls performing overhead club swings simultaneously could freeze or
  reset the game — fixed.**
- **Enemy-inventory Moonstone/invalid-item displays could freeze or Guru the
  game — fixed.**
- **Various other bug and stability fixes.**

See [CHANGES.md](CHANGES.md) for the historical sources and the distinction
between original-game, disk-revision, and port-runtime fixes.

## Game revisions and disk provenance

The SPS-preserved boxed-retail disk set used for comparison contains a game
engine that identifies itself in-game as **v1.4**. The commonly circulating
Crystal-cracked ADF set contains a structurally different engine revision with
**no numeric version tag**. It should therefore be called the *Crystal cracked
build*, not v1.0, v1.3, or any other guessed version.

The port detects that common ADF lineage and applies a default retail-parity
layer. This brings verified fixes, rules, balance, and behaviour into line with
the retail v1.4 reference where practical. Some additional safeguards go beyond
retail v1.4 where its code remains fragile.

## The removed disease/curse

The original game contains a designed disease mechanic. Its handler drains hit
points and sets a hidden flag that removes one life at every day-end until the
healer clears it. The manual broadly warns that Ratmen carry a deadly disease
and recommends treatment, but the repeating life drain has no visible status or
useful in-game feedback, and the exact event selector proved erratic in testing.

This port deliberately removes that mechanic: the disease handler is suppressed
and stale disease flags in existing saves are cleared. This is a gameplay choice,
not an original-game bug fix. Math the Wizard's separate, temporary illness
remains unchanged.

## Game data

The copyrighted Mindscape game disks, modules, and artwork are not included.
Supply your own three original disk images as
`Moonstone ... Disk1/2/3.adf`; the runtime extracts the required modules
automatically. Boxed-disk SPS/KryoFlux preservation images are the preferred
reference when available.

## Layout

| Path | What |
|------|------|
| `recomp/src/moon.c` | OCS runtime, host harness, and compatibility fixes. |
| `recomp/src/loader.c` | Module loader and AmigaOS exec/dos HLE. |
| `recomp/build.sh` | Builds the Windows executable with Zig. |
| `recomp/RE_NOTES.md` | Detailed reverse-engineering notes. |
| `recomp/tools/` | Reverse-engineering, audio, and image tools. |

## Building

Third-party dependencies are downloaded separately:

- Zig 0.16.0 in `recomp/tools/zig-x86_64-windows-0.16.0/`
- SDL2 2.32.10 in `recomp/vendor/SDL2/`
- Musashi 68000 core in `recomp/vendor/Musashi-master/`

Then run:

```sh
bash recomp/build.sh
```

The executable and SDL2 runtime are written to `recomp/build/`.

## License

Copyright © 2026 Undine1. The native runtime is licensed under the
[GNU General Public License v3.0](LICENSE). The original game code, data, and
artwork remain © 1991 Mindscape International / Rob Anderson and are not
distributed here.
