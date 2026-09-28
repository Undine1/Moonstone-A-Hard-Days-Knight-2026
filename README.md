# Moonstone 2026

Prebuilt native Windows port of *Moonstone: A Hard Days Knight*
(Mindscape, 1991). The original Amiga game runs through an embedded 68000 core
and custom OCS implementation, with no external emulator or Kickstart ROM.

**v1.4.0 (+ Local Multiplayer)** supports two-player Practice and up to four
campaign players through the original game's selection, turns and combat.
Campaign players can share fewer controllers. Each knight's initial choice is
reused automatically on later turns, including after day changes.
One campaign player can choose keyboard with Enter, even with extra controllers;
everyone can still type a custom knight name. A disconnected normal controller
must be reclaimed with Start; the game never switches players to keyboard automatically.
Reconnect the missing pad or use an unassigned replacement; recovery preserves
other players' controller assignments and existing sharing groups.
When two opponents share a controller, one can explicitly borrow an unassigned
keyboard with Enter for their duel and loot, then resume their usual controller.
Choose **Practice** directly: P1 keeps the menu controller and P2 gets the next
available controller, or the keyboard, without an extra Start prompt. Unknown
owners and reconnects still require identification. The campaign's Players
setting does not affect Practice. See [the multiplayer guide](recomp/MULTIPLAYER.md).

Numbered map choices use Up/Down to highlight an option and Attack/Select to
confirm. Number keys and numpad keys also work on the keyboard player's turn.

The release includes **Moonstone Singleplayer.exe** and **Moonstone Multiplayer.exe**
to keep separate F5/F9 saves: `saves/Singleplayer/singleplayer.sav` and
`saves/Multiplayer/multiplayer.sav`. Opening `moonstone.exe` directly uses the
singleplayer file. Set Players in the game menu as usual. To keep an old
`moonstone.sav`, close the game, keep a backup, rename it to the appropriate
filename and place it in the matching folder without overwriting an existing
save. Earlier beta saves in the root directory also need to be placed there.

## Download and setup

1. Open the [Releases page](https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026/releases).
2. Download **`Moonstone-2026-Windows-x64.zip`**.
3. Extract the complete ZIP.
4. Grab Moonstone Disk1, Disk2, Disk3 ADF files on **`wowroms`** and extract them
   into the **`data`** folder, named `Disk1.adf`, `Disk2.adf` and `Disk3.adf`.
5. Open **`Moonstone Singleplayer.exe`** or **`Moonstone Multiplayer.exe`**.
   Each launcher selects its own save folder; choose the player count in the game.

## Notes

- This build requires 64-bit Windows.
- Disk files should be sourced from wowroms or they may not work.
- Keyboard and controller bindings can be changed in `controls.ini` (optional).
- Quicksave and Quickload anywhere, including during combat. (F5/F9)
- Skip intro: Press Space / Enter / Ctrl or
  A / B / LB / RB / RT on controller.

See [RELEASE-NOTES.md](RELEASE-NOTES.md) for the update list and save compatibility
notes. Keep a backup when importing an old save: some corrections need a new
campaign, and v1.4 saves cannot be loaded by v1.3 or earlier.

## Highlights

- Automatic disk swapping with no interruption.
- Faithful graphics and Paula audio through the custom OCS runtime.
- Bug fixes.

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

This release also restores rival knights' retail XP:
one point every four in-game daybreaks while alive, plus their normal point
for a combat victory. XP pays for stat upgrades and is separate from actual
Moonstone tokens. Existing campaigns receive these future rewards normally.

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
