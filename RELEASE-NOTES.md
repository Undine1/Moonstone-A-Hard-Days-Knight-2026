# Moonstone 2026 v1.4.0 (+ Local Multiplayer)

## Major updates

- Local multiplayer: two players in Practice and up to four in a campaign,
  with controllers, keyboard and shared devices supported.
- The game now has 2 launchers. They are identical. Their only purpose is to
  separate your single & multiplayer saves. Save games now live in
  `saves/Singleplayer/singleplayer.sav` and `saves/Multiplayer/multiplayer.sav`.

**Important notes:**

- Old saves still load, but some corrections need a new campaign, including the
  starting lair enemies and the one-per-campaign Sword of Sharpness limit.

**Modernized input handling for multiplayer**

Up to 4 players can use their own controller/keyboard, or share them if needed.
At least 2 input devices are recommended for multiplayer, because PvP requires
a separate device for each fighter. If two players sharing a device attack each
other, one is prompted to choose a different device for the fight.

You can technically all share one input device and avoid PvP. However, if the
keys needed to reach the Guardian end up split between players, you will need
a second input device to fight for them.

## QoL updates

- Improved map choice menus: use Up/Down to highlight an option, then
  Attack/Select to confirm. All choices are accessible by controller, including
  menus with three or more options. Number shortcuts remain available to the
  keyboard player.

## Bug fixes

- Fixed ratmen appearing in place instead of entering from the screen edge.
- Fixed AI knights missing XP for combat victories.
- Limited lair victory XP to one reward per lair, preventing repeat farming by
  leaving treasure behind.
- Fixed the version display blocking gameplay. V now toggles the display on/off.
- Improved detection of damaged disks, incomplete startup files and failed reads.

## Retail Parity fixes

- Restored retail weapon and armour looting, fixing crashes, duplicated swords,
  incorrect transfers and health calculations.
- Limited Sword of Sharpness generation to one per campaign.
- Restored retail handling of ratman killing sequences and death cleanup.
- Restored retail AI-knight block recovery and defensive decisions.
- Corrected the life-icon display for eliminated knights to match retail.
- Restored retail confirmation sounds for purchases, sales, looting, city
  choices and accepting a knight's name.
- Restored retail handling of Stonehenge offerings. Invalid offerings
  (e.g. daggers) can't be selected anymore.
- Restored handling of overlapping impacts from Balok & Troll impact screen-shakes.
- Corrected the dragon's close-range foot-strike damage.
- Restored the knight's dragon-fire death animation timing.
- Restored one XP every four in-game days for living AI knights.
- Restored random-number updates while in-game menu cursors are active.
- Corrected the starting enemies and encounter counts in three northwest
  grassland lairs.
