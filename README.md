# Moonstone Reborn

Ready-to-play native Windows port of *Moonstone: A Hard Days Knight*
(Mindscape, 1991). The original Amiga game runs through an embedded 68000 core
and custom OCS implementation, with no external emulator or Kickstart ROM.

## Download and play

1. Open the [Releases page](https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026/releases).
2. Download **`Moonstone-Reborn-Windows-x64.zip`**. Do not download GitHub's
   automatically generated "Source code" archives.
3. Extract the complete ZIP, then double-click **`moonstone.exe`**.

That is all. There is no installer, command line, build process, emulator, or
ROM setup. The package is portable and requires 64-bit Windows. Keep
`moonstone.exe`, `SDL2.dll`, and the `data` folder together.

The included `README.txt` contains the controls and a short troubleshooting
note.

## Highlights

- Automatic disk swapping with no interruption.
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

## Game revisions

The SPS-preserved boxed-retail reference contains a game engine that identifies
itself as **v1.4**. The commonly circulating Crystal-cracked ADF engine differs
structurally and has **no numeric version tag**; it should not be assigned a
guessed version number.

The port's default retail-parity layer brings verified fixes, rules, balance,
and behaviour into line with the retail v1.4 reference where practical. Some
additional safeguards go beyond retail v1.4 where its code remains fragile.

## The removed disease/curse

The original game contains a designed disease mechanic. Its handler drains hit
points and sets a hidden flag that removes one life at every day-end until the
healer clears it. The manual warns that Ratmen carry a deadly disease and
recommends treatment, but the repeating life drain has no visible status or
useful in-game feedback, and the exact event selector proved erratic in testing.

This port deliberately removes that mechanic. This is a gameplay choice, not an
original-game bug fix. Math the Wizard's separate temporary illness remains
unchanged.

## License

Copyright © 2026 Undine1. The native runtime source is licensed under the
[GNU General Public License v3.0](LICENSE). The original game code, data, and
artwork remain © 1991 Mindscape International / Rob Anderson.
