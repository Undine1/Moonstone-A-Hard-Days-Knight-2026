# Moonstone 2026 — Changes from the 1991 Amiga game

_Last updated: 2026-09-06._

## v1.2.0

- Practice's second knight stays idle without player-two input. Mouse movement,
  menu controls, and player-one attacks no longer make it wander or attack.
  The same input isolation applies to player-two knights in campaign duels.
- Added regression checks for practice movement, attacks, damage, and save/load.
  The port still supports one player's controls; separate second-player controls
  are not included in this release.

## v1.1.0

This release adds controller status overlays and native error dialogs, removes
the separate command window, and corrects the fatal canopy-choke death display.

## Window and controller status

- The game opens without a separate command window.
- A four-second overlay reports controller connections and disconnections,
  including controllers already connected at startup. Details remain in the log.
- Disconnecting an unused controller leaves the active controller connected;
  disconnecting the active one switches to another available controller.
- Startup-data and save-loading failures show an error dialog and write details
  to the log. Audio-device, recording, and log-file failures show a warning.
- Recording warnings stay above the game window until dismissed.

## Combat feedback

- A fatal canopy choke now keeps the knight's death pose visible until the
  inventory opens. The dead knight can no longer resume stabbing upward.

## Confirmed historical Amiga bug fixes

- **Fixed: Two trolls performing overhead club swings simultaneously crashed
  the game.**
- **Fixed: A bugged Moonstone appearing in an enemy inventory crashed the
  game.**
- **Various other bug fixes.**

Historical reports:

- [Lemon Amiga report (14 April 2012)](https://www.lemonamiga.com/forum/viewtopic.php?t=6169)
  records the simultaneous overhead-club crash.
- [The Company forum (2010)](https://forum.thecompany.pl/general-talk/moonstone-t490.html)
  records the black-knight Moonstone inventory Guru.
- [Moonstone: A bug hunter's guide (2015)](https://rufusplaysgames.wordpress.com/2015/07/07/moonstone-a-bug-hunters-guide/)
  independently documents both the tandem-troll and Moonstone inventory failures.
- The [official WHDLoad install notes](https://www.whdload.de/games/Moonstone.html)
  likewise describe removing various access faults and bugs from supported Amiga
  releases.

## Retail v1.4 and the common ADF release

The SPS-preserved boxed-retail reference (`.IPF` format) contains a game engine
that identifies itself as **v1.4**. The commonly circulating `.ADF` release
differs structurally and has **no numeric version tag**.

This port (*Moonstone 2026*) brings fixes, rules, balance, and behaviour into
line with the retail v1.4 reference where practical. Some additional safeguards
go beyond retail v1.4 where its code remains fragile.

## Deliberate change: disease/curse removed

The original game contains a disease mechanic. The disease drains hit points
and removes one life at every day-end until the healer clears it. The manual
warns that Ratmen carry a deadly disease and recommends treatment, but there is
no mention of the specifics, nor is there any in-game feedback about this. I
personally had no idea what was happening and thought it was a bug, and from the
videos I watched, other people had the same experience. So I've decided to
remove this feature from the game.

Manual reference: [Moonstone Amiga manual](https://www.lemonamiga.com/doc/moonstone-a-hard-days-knight/1109).
