# Moonstone 2026

Prebuilt native Windows port of *Moonstone: A Hard Days Knight*
(Mindscape, 1991). The original Amiga game runs through an embedded 68000 core
and custom OCS implementation, with no external emulator or Kickstart ROM.

## Download and setup

1. Open the [Releases page](https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026/releases).
2. Download **`Moonstone-2026-Windows-x64.zip`**.
3. Extract the complete ZIP.
4. Grab Moonstone Disk1, Disk2, Disk3 ADF files on **`wowroms`** and extract them into the **`Data`** folder
5. Double-click **`moonstone.exe`**.

- This build requires 64-bit Windows. 
- Disk files should be sourced from wowroms or they may not work.
- Keyboard and controller bindings can be changed in `controls.ini` (optional)
- Quicksave and Quickload anywhere, including during combat. (F5/F9)
- Skip intro: Press Space / Enter / Ctrl or
  A / B / LB / RB / RT on controller.
- `README.txt` contains the default controls and troubleshooting notes.

## Highlights

- Automatic disk swapping with no interruption.
- Faithful graphics and Paula audio through the custom OCS runtime. 
- Bug fixes

## Confirmed original Amiga bugs fixed

- **Fixed: Two trolls performing overhead club swings simultaneously crashed
  the game.**
- **Fixed: A bugged Moonstone appearing in an enemy inventory crashed the
  game.**
- **Various other fixes.**

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
no mention of the specifics, nor is there any in-game feedback about this curse. I
personally had no idea what was happening and thought it was a bug, and from the
videos I watched, other people had the same experience. So I've decided to
remove this feature from the game to spare everyone from similar confusion.

## Final notes

Multiplayer is currently not functional, since this release only supports 1 active controller/keyboard. I could add this if there is demand for it. 

If you enjoyed Moonstone 2026 and completed a playthrough without encountering
any bugs, you are welcome to buy me a coffee:

- **EVM:** `0x759500A80C17978df1B92d2497A80786290115c2`
- **BTC:** `3FF42zRE1qAdwmcoLvuRGa3FEZeYW1LyYi`
- **Solana:** `AwaQbXeYnAKhszJ4Y4cCY6ApBqvkSoCZLDgczPjogynb`

## License

Copyright © 2026 Undine1. The native runtime source is licensed under the
[GNU General Public License v3.0](LICENSE). The original game code, data, and
artwork remain © 1991 Mindscape International / Rob Anderson. Third-party
components and notices are listed in `THIRD-PARTY-NOTICES.txt` in the release.
