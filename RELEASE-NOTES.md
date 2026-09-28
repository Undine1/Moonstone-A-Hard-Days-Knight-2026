# Moonstone 2026 v1.4.0 (+ Local Multiplayer)

**Important notes:**

- Saves now live in `saves/Singleplayer/singleplayer.sav` and
  `saves/Multiplayer/multiplayer.sav`, instead of `moonstone.sav` in the game folder.
- To import an old save, close the game, keep a backup, rename a copy to the
  appropriate filename and place it in that save folder.
- Old saves still load, but some corrections need a new campaign, including the
  starting lair enemies and the one-per-campaign Sword of Sharpness limit.
  Old saves also cannot reconstruct previous lair XP rewards.
- Saves written by this build cannot be loaded by v1.3 or earlier. Keep your
  original save if you want to return to an older version.
- Each launcher selects its save folder. Set the number of players in the game
  menu; renaming a save does not change the campaign's player count.

## Major updates

- Local multiplayer: two players in Practice and up to four in a campaign,
  with controllers, keyboard and shared controllers supported.
- Separate Singleplayer and Multiplayer launchers, each with its own savegame
  folder.

## Bug fixes

- Fixed controller selection in numbered menus with three or more choices.
- Fixed ratmen appearing in place instead of entering from the screen edge.
- Fixed ratman killing-sequence and death-cleanup issues.
- Fixed weapon and armour looting crashes, sword duplication and incorrect item
  transfers or health calculations.
- Fixed AI-knight block recovery and defensive decisions.
- Fixed AI knights missing XP for combat victories.
- Fixed dead knights incorrectly displaying five life icons.
- Restored missing confirmation sounds for purchases, sales, looting, city
  choices and accepting a knight's name.
- Fixed invalid Stonehenge offerings granting rewards or corrupting inventory.
  Valid offerings retain their life and health rewards; they do not raise stats.
- Restored missing Balok and Troll impact effects and corrected overlapping
  effect handling.
- Fixed the version display blocking gameplay. Press V again to hide it.
- Improved detection of damaged disks, incomplete startup files and failed reads.
- Fixed saving and loading during restored sounds, ratman killing sequences and
  XP rewards.

## Retail Parity fixes

- Limited Sword of Sharpness generation to one per campaign.
- Corrected the dragon's close-range foot-strike damage.
- Restored the knight's dragon-fire death animation timing.
- Restored one XP every four in-game days for living AI knights.
- Limited lair victory XP to one reward per lair, preventing repeat farming by
  leaving treasure behind.
- Restored random-number updates while in-game menu cursors are active.
- Corrected the starting enemies and encounter counts in three northwest
  grassland lairs.
