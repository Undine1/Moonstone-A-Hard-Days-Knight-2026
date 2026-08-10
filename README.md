# Moonstone Reborn

Prebuilt native Windows port of *Moonstone: A Hard Days Knight*
(Mindscape, 1991). The original Amiga game runs through an embedded 68000 core
and custom OCS implementation, with no external emulator or Kickstart ROM.

## Download and setup

1. Open the [Releases page](https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026/releases).
2. Download **`Moonstone-Reborn-Windows-x64.zip`**. Do not download GitHub's
   automatically generated "Source code" archives.
3. Extract the complete ZIP.
4. Supply your own ADF files into the included `data` folder. The ADF files
   should come in a batch of three disk files and should be named specifically:
   - `Disk1.adf`
   - `Disk2.adf`
   - `Disk3.adf`
5. Double-click **`moonstone.exe`**.

Each ADF must be the standard 901,120-byte size. The ADF files are not included.
On first launch, the runtime extracts the required boot modules from Disk 1
automatically. There is no installer or build process. The package
is portable and requires 64-bit Windows; keep `moonstone.exe`, `SDL2.dll`, and
the `data` folder together.

The included `README.txt` contains the controls and a short troubleshooting
note. Keyboard and controller bindings can be changed in `controls.ini` beside
the executable; changes take effect after restarting the game.

## Highlights

- Automatic disk swapping with no interruption.
- Skippable intro — by default, press Space, Enter, or Ctrl on the keyboard, or
  A, B, LB, RB, or RT on a controller.
- Complete and configurable keyboard and game-controller support, with optional
  mouse control.
- Quicksave and quickload anywhere, including during combat.
- Faithful graphics and Paula audio through the custom OCS runtime.

## Confirmed original Amiga bugs fixed

- **Fixed: Two trolls performing overhead club swings simultaneously crashed
  the game.**
- **Fixed: A bugged Moonstone appearing in an enemy inventory crashed the
  game.**

See [CHANGES.md](CHANGES.md) for the historical sources.

## Game revisions

The SPS-preserved boxed-retail reference (`.IPF` format) contains a game engine
that identifies itself as **v1.4**. The commonly circulating `.ADF` release
differs structurally and has **no numeric version tag**.

This port (*Moonstone 2026*) brings fixes, rules, balance, and behaviour into
line with the retail v1.4 reference where practical. Some additional safeguards
go beyond retail v1.4 where its code remains fragile.

## The removed disease/curse

The original game contains a disease mechanic. The disease drains hit points
and removes one life at every day-end until the healer clears it. The manual
warns that Ratmen carry a deadly disease and recommends treatment, but there is
no mention of the specifics, nor is there any in-game feedback about this. I
personally had no idea what was happening and thought it was a bug, and from the
videos I watched, other people had the same experience. So I've decided to
remove this feature from the game.

## License

Copyright © 2026 Undine1. The native runtime source is licensed under the
[GNU General Public License v3.0](LICENSE). The original game code, data, and
artwork remain © 1991 Mindscape International / Rob Anderson. Third-party
components and notices are listed in `THIRD-PARTY-NOTICES.txt` in the release.
