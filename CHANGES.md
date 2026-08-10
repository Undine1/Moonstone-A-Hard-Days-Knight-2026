# Moonstone Reborn — Changes from the 1991 Amiga game

_Last updated: 2026-08-10._

## Confirmed historical Amiga bug fixes

- **Fixed: Two trolls performing overhead club swings simultaneously crashed
  the game.**
- **Fixed: A bugged Moonstone appearing in an enemy inventory crashed the
  game.**

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
