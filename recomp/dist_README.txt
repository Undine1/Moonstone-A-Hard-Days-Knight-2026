================================================================
  MOONSTONE - A Hard Days Knight         native Windows port
================================================================

Moonstone 2026 v1.4.1.
ADF files are not included. This release supports two-player Practice and
campaigns with up to four players, including shared controllers.


---------------------------------------------------------------
 HOW TO PLAY
---------------------------------------------------------------

1. Extract the complete ZIP.
2. Supply your own ADF files into the included data folder. The ADF files
   should come in a batch of three disk files and should be named specifically:

       Disk1.adf
       Disk2.adf
       Disk3.adf

3. Double-click:

       Moonstone Singleplayer.exe    (uses saves/Singleplayer/singleplayer.sav)
   or  Moonstone Multiplayer.exe    (uses saves/Multiplayer/multiplayer.sav)

Opening moonstone.exe directly uses the Singleplayer save folder. The launch choice
selects save storage; set Players in the game menu as usual. The window title
shows the save choice, which stays fixed until the game is closed, including
when playing Practice. Keep both launchers beside moonstone.exe.

On first launch, the game extracts its required boot modules from
Disk1.adf automatically. Each ADF must be 901,120 bytes. Requires
64-bit Windows. Keep moonstone.exe, SDL2.dll, and the data folder
together. The folder is portable.

The game opens without a separate command window. A brief on-screen message
shows when a controller connects or disconnects, including one already
connected at startup. Diagnostic details are saved in moonstone.log.

UPDATING FROM AN EARLIER VERSION
Extract this release into a new folder and copy your own three ADFs into it.
Keep a backup of your previous installation and saves. Each installation has
its own log, saves, and controls.ini. See RELEASE-NOTES.md for the update list
and save compatibility notes. Start a new campaign for the restored retail rule
that only one Sword of Sharpness can be generated per campaign. Older saves
still load, but their existing inventories and generation history are not
repaired. New saves also preserve the ratman's killing sequence through
quicksave and restart. Saves written by this build require this build or newer.

The dragon's close-range foot strike now matches retail damage. This correction
also applies to existing campaigns after restarting the game and loading a save.
AI knights also use retail's block recovery and defensive decisions. Human
controls and blocking rules are unchanged; no new campaign is required.

Each lair now awards victory XP only once per campaign, shared by all players,
as in retail. Uncollected treasure remains available on a return visit. New
saves remember which lairs paid XP. Older saves cannot recover that history;
each remaining lair can award XP once more before the new guard takes effect.

Living rival knights now earn retail's one XP every four in-game daybreaks
and their normal XP for winning a fight. Existing saves work immediately.
XP buys stat upgrades; it is separate from actual Moonstone ownership.

Dead knights now show zero life icons when their lives fall below zero,
matching retail. This is a display correction and works with existing saves.

Multiplayer scene changes use the game's normal input handling, with no extra
release-controls pause or message. Player identification consumes only its
Start/Enter press. Existing saves work.


---------------------------------------------------------------
 CONTROLS
---------------------------------------------------------------

  Edit controls.ini beside moonstone.exe and restart the game.
  Deleting it restores the defaults shown below.

  MAIN CONTROLLER
    The first controller you use to navigate or skip the intro owns the menus.
    Other pads cannot take over while it is connected. Connection order does
    not decide ownership. After disconnecting it, release and use the pad you
    want instead. Keyboard and mouse remain available in solo play.
    Multiplayer's explicitly claimed Player 1 also owns the menu on return.

  CAMPAIGN
    Set Players to 2, 3 or 4, then choose Select Knight. Player numbers follow
    selection order, regardless of the colour each player chooses. Solo play
    has no controller setup popup.

    Each player chooses a device when first needed, normally at their knight's
    selection turn: Start on their controller, or Enter for keyboard. Any number
    of players can choose keyboard, even with four controllers connected.
    Extra controllers remain unassigned. If everyone chooses a different device, it follows their
    knight through turns, towns, inventory and combat.

    Examples: three players can use two controllers plus keyboard. Four players
    can use four controllers, or three controllers plus keyboard.
    Keyboard typing works for EVERYONE'S custom knight name, even if another
    player owns keyboard gameplay. Text goes to the active name field without
    changing device assignments. Outside name entry, gameplay keys and mouse
    actions belong to the active keyboard player.

    The prompt stays visible until the requested player chooses or confirms
    their device. It offers Start or Enter while keyboard is available, then
    only the applicable action. Existing assignments stay intact on confirmation.

    To share a device, choose the same controller with Start or the same keyboard
    with Enter during knight selection. Sharing is allowed even with spare
    controllers connected. Each knight's choice is reused automatically on later
    turns and day changes. Only the active knight receives shared-device input.

    Two human opponents need different devices in a duel. If both normally use
    the same pad, one can press Start on another controller or Enter to borrow
    the keyboard for that fight, including an idle third player's keyboard.
    This requires an explicit choice; a fighter's keyboard cannot be borrowed.
    The winner keeps their device for looting; usual choices resume on the map,
    including all players sharing the keyboard. If both fighters normally share
    the keyboard, one presses Start on a controller for the duel and loot.
    A keyboard-only campaign waits for a controller to be connected for PvP.
    One controller plus keyboard can serve up to four campaign players.
    Monster fights require only the participating player's device.

    Restarting and loading asks for identification again. Device IDs are not
    saved. F9 keeps current assignments and resumes normally. A required
    disconnected controller pauses for recovery; reconnect and press Start.
    An unassigned replacement controller is also allowed. A controller belonging
    to another player or sharing group cannot replace the missing pad. The
    connection notification alone does not assign a controller: press Start
    when the game asks that player to identify it.
    Enter cannot replace a disconnected normal controller. Missing inactive players
    reclaim when needed. Once everyone has chosen, extra pads cannot change
    sharing mode or replace a campaign keyboard player. One fresh reconnect
    claim restores the missing shared pad for all players who chose it.

  PRACTICE
    Choose Practice directly. It always has two fighters; the campaign Players
    setting does not affect it. P1 keeps the menu controller; P2 gets the next
    available controller or keyboard. Normal entry needs no setup prompt.

    Keyboard only: blue knight moves, green stays idle, no assignment popup.
    Two players: P1 is blue; P2 is green. At least one controller is needed.
      One controller ........ P1 controller, P2 keyboard
      Two controllers ....... one each, keyboard gameplay unassigned
      More than two ......... P1 menu controller, P2 next available controller
    Mouse clicks belong to the keyboard's player. Both pads use the same
    controls.ini profile. Start identifies controllers in the popup.

    With no known menu controller, restarting and loading asks P1 to press Start
    once; P2 is assigned automatically. F9 within a session retains assignments
    and resumes normally. Old Practice
    saves work regardless of their campaign Players setting. Solo campaign has
    no assignment popups.

    In multiplayer, disconnecting a player's controller pauses the game. Press
    Start on the reconnected pad or an unclaimed replacement. Enter does not
    offer keyboard fallback. The other player's device stays assigned.
    Connecting the first pad during
    keyboard-only Practice opens Player 1 setup. Adding a pad during a round
    keeps the current assignments; start Practice again from the menu to use it.
    Losing window focus also pauses multiplayer. F11 works while waiting;
    Esc leaves fullscreen, or quits if already windowed.

    Original combat pause: Start on either assigned pad or Space on keyboard.
    Host shortcuts (pause, save/load, quit, diagnostics) remain keyboard-accessible.

  SKIP THE INTRO
    Press Space, Enter, or Ctrl on the keyboard, or A, B, LB, RB, or RT
    on a controller.

  GAME CONTROLLER (Xbox / generic XInput pad)
    Left stick / D-pad . move / move the pointer
    A / RB / LB / RT ... attack / select / confirm
    Y ................... open inventory on the map
    Start ............... pause / resume during combat
    Select / Back ........ rest / end the current turn (pass the day)

  KEYBOARD (no mouse required)
    Arrow keys ........... move / move the pointer
    Ctrl / Enter /
      Numpad Enter ....... attack / select / confirm
    Space or I ........... open inventory on the map
    Space (in combat) .... pause / resume
    E ..................... rest / end the current turn
    Q ..................... abandon the current quest / return to setup
    V ..................... toggle the version/revision credit on the map
    F11 ................... toggle fullscreen (also works during setup prompts)
    Esc ................... leave fullscreen; quit if already windowed

  MOUSE (optional)
    Mouse movement ........ move the pointer
    Left-click ........... attack / select / confirm

  NAME ENTRY
    Type a name with the keyboard. Backspace edits; Enter or Numpad Enter confirms.

  SAVE / LOAD
    F5 .................... quicksave anywhere, including combat
    F9 .................... quickload the last quicksave

    F5 and F9 use the file chosen at launch, under the game's saves folder:
      Singleplayer: saves/Singleplayer/singleplayer.sav
      Multiplayer:  saves/Multiplayer/multiplayer.sav
    Each choice keeps one quicksave. F9 never loads the other file instead.
    Both folders are included; missing folders are recreated when saving.
    If Windows blocks folder creation, SAVE FAILED appears and the log names
    the folder and Windows error. No save is written to a different location.

    To keep an existing moonstone.sav, close the game and rename it to the
    appropriate filename and put it in the matching folder above. Earlier beta
    saves named singleplayer.sav or multiplayer.sav also belong in those folders.
    Keep a backup; do not overwrite a newer save. The game does not move or
    rename old saves automatically, and no longer loads saves from the root.

  SELECTION POPUPS
    Use Up/Down to highlight any option, then Attack/Select to confirm
    (controller A by default). The list wraps at either end.
    On the keyboard player's turn, number keys 1-9 or numpad 1-9
    also select an option directly. Keyboard ownership does not change.


---------------------------------------------------------------
 TROUBLESHOOTING
---------------------------------------------------------------

If the game does not open, check that all three files are directly in
the data folder and named Disk1.adf, Disk2.adf, and Disk3.adf. Also
make sure SDL2.dll is still next to moonstone.exe.

If startup data or a requested save cannot be loaded, an error dialog names
the file. Details are written to moonstone.log (or the path given by --log).
Warnings also appear if sound, audio recording, or the log file is unavailable.

For setup errors, the log lists the operating system and 32/64-bit architecture,
disk paths and exact sizes, missing startup files, extraction failures, and
operating-system read/write errors. Send the
log immediately after the failed launch when reporting a problem.

Project page and native runtime source:
https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026

MOONSTONE (c) 1991 Mindscape International / Rob Anderson.
Native runtime (c) 2026 Undine1, licensed under GPL v3.
