# Moonstone Reborn — Changes from the 1991 Amiga game

_Last updated: 2026-08-09._

This is intentionally a short public summary. Low-level implementation details,
addresses, diagnostics, and individual port-runtime corrections belong in the
source and reverse-engineering notes, not in a claim that every fix was an
original-game defect.

## Confirmed historical Amiga bug fixes

These are the bugs this project publicly calls original-game bugs. They were
described by Amiga players years before this port; the reports do not establish
that every disk revision behaved identically.

- **Two-troll overhead-swing freeze/reset — fixed.** Two trolls performing the
  overhead club attack together could freeze the game or reset the Amiga.
- **Enemy-inventory Moonstone/invalid-item crash — fixed.** Moonstone entries
  appearing in another character's inventory could Guru, freeze, or crash the
  game when hovered over or taken.
- **Various other bug and stability fixes.** These are deliberately not all
  presented as original-game bugs: some repair the common cracked data revision,
  and others correct or harden the native port itself.

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

## Retail v1.4 and the common cracked ADFs

Direct comparison of the preserved game modules established two distinct engine
revisions:

- The SPS-preserved boxed-retail reference contains an in-game **v1.4** tag.
- The common Crystal-cracked ADF engine has **no numeric version tag** and differs
  structurally from the retail engine. No evidence supports calling it v1.0,
  v1.3, or another guessed number.

The WHDLoad install/slave also has its own version numbers; those are unrelated
to the game's v1.4 engine tag. WHDLoad confirms that three game releases exist,
which is another reason not to treat every Amiga disk image as identical.

The port's default retail-parity layer translates verified retail v1.4 repairs
and gameplay differences onto the common Crystal ADF revision. This reproduces
verified retail behaviour across shopping, combat state, AI, damage, loot, and
other gameplay paths. Additional safety fixes go beyond retail v1.4 where
appropriate; this is targeted compatibility work, not a wholesale replacement
with the retail binary.

## Deliberate change: disease/curse removed

The disease/curse is designed original gameplay, not a port bug. Its handler
drains hit points and arms a hidden flag that removes one life at each day-end
until the healer clears it. The original manual warns that Ratmen carry a deadly
disease and recommends prompt treatment, but the repeating life loss has no
visible status or clear in-game explanation.

The port intentionally suppresses the disease handler and clears an already-set
flag from existing saves at the next day-end. It was removed because its hidden,
repeating penalty appeared to players as random life loss, and its event
selection behaved erratically during investigation. Math the Wizard's separate
temporary illness is retained.

Manual reference: [Moonstone Amiga manual](https://www.lemonamiga.com/doc/moonstone-a-hard-days-knight/1109).

## Other port changes

- Native Windows runtime with an embedded 68000 core and custom OCS graphics,
  audio, input, and disk support; no external emulator or Kickstart ROM needed.
- Automatic disk swapping, faster file-backed loading, keyboard/mouse/controller
  support, and quicksave/quickload.
- Numerous graphics, audio, timing, persistence, compatibility, and defensive
  runtime fixes.

The detailed engineering history remains in `recomp/RE_NOTES.md` and the source
comments for maintainers who need it.
