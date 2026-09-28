# Moonstone 2026

Native Windows port of *Moonstone: A Hard Days Knight* (Mindscape, 1991),
with local multiplayer. No external emulator or Kickstart ROM required.

## Quickstart

1. Download **`Moonstone-2026-Windows-x64.zip`** from the
   [latest release](https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026/releases/latest)
   and extract the complete ZIP.
2. Grab all 3 Moonstone ADF files from **wowroms** and place them in the
   **`data`** folder.
3. Run **`Moonstone Singleplayer.exe`** or **`Moonstone Multiplayer.exe`**.

- The 2 launchers are identical. Their only purpose is to separate your single & multiplayer saves.
- The intro is skippable.
- The first controller used during startup becomes the main controller and Player 1 in Practice.
- ADF files should be sourced from **wowroms** or they may not work.
- This port requires 64-bit Windows.

## Basic controls

| Action | Keyboard | Controller |
|---|---|---|
| Move | Arrow keys | Left stick / D-pad |
| Attack / select | Ctrl / Enter | A |
| Pass turn | E | Back / Select |
| Open inventory | I / Space | Y |
| Pause combat | Space | Start |
| Quicksave / quickload | F5 / F9 | Use keyboard |

Check [controls.ini](recomp/controls.ini) for all bindings and detailed setup.

## Local multiplayer

Up to 4 players can use their own controller/keyboard, or share them if needed.
At least 2 input devices are recommended for multiplayer, because PvP requires
a separate device for each fighter. If two players sharing a device attack each
other, one is prompted to choose a different device for the fight.

You can technically all share one input device and avoid PvP. However, if the
keys needed to reach the Guardian end up split between players, you will need
a second input device to fight for them.

## Highlights

- Automatic disk swapping with no interruption.
- Faithful graphics and Paula audio through the custom OCS runtime.
- Modernized input handling.
- Save / Load functionality.
- Improved selection menus when multiple map encounters overlap.
- Extensive bug fixes (including all known crashes).
- Gameplay reflects latest official release of the game

## Game revisions

The SPS-preserved boxed-retail reference (`.IPF` format) contains a game engine
that identifies itself as **v1.4**. The commonly circulating `.ADF` release
differs structurally and has **no numeric version tag**.

This port (*Moonstone 2026*) brings fixes, rules, balance, and behaviour into
line with the retail v1.4 reference where practical. Some additional safeguards
go beyond retail v1.4 where its code remains fragile.

## The removed disease/curse

The original game contains a disease mechanic. The disease drains hit points
and removes one life at each four-day periodic update until the healer clears it. The manual
warns that Ratmen carry a deadly disease and recommends treatment, but there is
no mention of the specifics, nor is there any in-game feedback about this curse. I
personally had no idea what was happening and thought it was a bug, and from the
videos I watched, other people had the same experience. So I've decided to
remove this feature from the game to spare everyone from similar confusion.

## Final notes

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
