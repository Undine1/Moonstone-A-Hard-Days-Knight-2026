# Local multiplayer — v1.4.0

This release supports two-player **Practice**
and two to four campaign players. It connects modern devices to the original
selection, turn, menu and combat routines. It does not enable network play.

Extract the release into a new directory and keep a backup of earlier saves.
Its log, quicksaves and controls file belong to that directory. The distribution includes
two small launchers alongside the game, with no game disks or saves.
Supply your own `data/Disk1.adf`, `Disk2.adf` and `Disk3.adf`.

Open **Moonstone Singleplayer.exe** for `saves/Singleplayer/singleplayer.sav`,
or **Moonstone Multiplayer.exe** for `saves/Multiplayer/multiplayer.sav`.
The `saves` folder is beside the game. F5 saves and F9 loads the chosen file.
The window title identifies the choice, which stays fixed until you close the
game. Opening `moonstone.exe` directly uses the Singleplayer save folder.
Keep both launchers beside `moonstone.exe`; shortcuts to them work too.
Both save folders are included in the package. Missing folders are recreated
when you save. A folder creation failure reports SAVE FAILED and logs the path
and operating-system error; saving never falls back to the game root.

The launch choice selects save storage. Set the number of campaign players in
the game's **Players** menu as usual; Practice does not change the save choice.
To keep an existing `moonstone.sav`, close the game and rename it to
`singleplayer.sav` or `multiplayer.sav` as appropriate, then put it in the matching
folder above. Saves from an earlier beta already using those filenames must
also be placed in the matching folder. Keep a backup and do not replace a newer
save. Old saves are never moved, renamed or loaded automatically, and F9 never
falls back to another folder or file.

Numbered map popups use the active player's Up/Down controls to highlight an
option and Attack/Select to confirm (controller A by default). Every listed
choice is reachable; the selection wraps at either end. On a keyboard-owned
turn, 1-9 or numpad 1-9 also select directly. Another player's keyboard, or an
unassigned keyboard, cannot choose for a controller-owned knight. Loading a
save inside this popup resets the highlight to the first choice and uses the
active map knight's device, even if the previous fight involved another knight.

The game checks disk contents and existing startup files on each launch. If an
error names a damaged disk, replace that image with a valid copy. If it says a
startup file does not match your disks, move that named file out of `data` and
restart to extract it again. Existing files are never overwritten by this check.
Details, OS information and the affected path are recorded in the log. Valid
cached data needs no write access; extracting missing files still requires it.

Start a new campaign for the restored retail limit of one generated Sword of
Sharpness per campaign. That history survives saving, quickloading and restarting.
Older saves still load, but their inventories/history are not migrated. New
saves require this build or newer.

New campaigns also use retail's enemy selection and starting budgets in three
northwest grassland lairs: one changes from wild beasts to spear Troggs, and two
gain their missing enemies. The original difficulty calculation still adjusts
the actual encounter to the knight. Existing campaigns keep their saved lair
state, including partially cleared encounters; start a new campaign to see this
correction.

The dragon's close-range foot strike uses retail's 10 base damage, with the
existing talisman reductions and minimum damage of 5. This correction also
applies to existing campaigns after restarting and loading a save.

The knight's death from dragon fire now uses retail's shorter collapse hold
(0.2 seconds instead of 0.8). Existing saves work; a save already partway through
the older hold finishes that in-progress animation normally.

Balok landings and Troll club impacts now display the original routine's brief
vertical movement. The original offsets and timing are preserved; overlapping
impacts keep the first effect's deadline, as in retail. Effect and menu-cursor
registration also handles intervening interrupts safely. Restart and load your
existing save to receive the correction; the save format remains v5.

AI knights now use retail's block recovery and defensive decisions, including
consecutive matching blocks. Human controls and blocking rules are unchanged.
This correction requires no new campaign or save-format change.

Lair victories now award XP once per lair for the entire campaign, as in retail.
All players share this reward history. Leaving treasure behind preserves the
lair and its loot, but repeat victories do not give more XP. Save format v5
remembers the history through F9 and restart. Older saves still load but cannot
reconstruct past rewards; each remaining lair can award XP once more.

AI knights earn one XP every four in-game daybreaks while alive, plus their
native combat-victory point. This matches retail and works with existing saves.
XP and actual Moonstone tokens are separate. The stat-upgrade price follows
the original Players setting: 3/2/1/1 XP for 1/2/3/4 human players. Token-looting
restrictions are unchanged.

## Playing

Scene changes handle disk confirmation automatically. Entering Math's tower or
being attacked should no longer stop on a black screen until Player 1 presses a
button. Restart this build and load your existing campaign to use the fix.

On the campaign map, press V to show "v1.4 - 2026 revision - Undine"; press V
again to hide it. The game and controls remain active. `show_version` in
`controls.ini` changes this binding and can assign a controller button. In
multiplayer, the active player's device controls it. Leaving the map or loading
a save dismisses the overlay. Saves made in the previous stuck version screen
resume normally with this build.

The active in-game menu cursor now advances the shared random-number sequence,
as in retail, even while stationary. Time spent in those menus can affect later
dice rolls, loot and AI decisions. This does not change gambling probabilities,
payouts or item effects. Existing saves retain their random-number state.

At Stonehenge, offer a potion, gem, ring, talisman or scroll to gain one life
(maximum five), restore health and remove a curse. Each offering consumes one
item. Ring and talisman offerings retain their labelled action even though
retail's click routing ignored them. Gold, daggers, equipment, the special sword,
keys and Moonstones are not offerings. Stats can be upgraded separately by
spending XP; offering an item never raises Strength, Constitution or Endurance.

Retail confirmation clicks play when choosing city destinations, buying or
selling equipment/items, taking a weapon and accepting a knight's name with
Fire or Enter. Main-menu navigation and rejected purchases retain their original
silence. Existing campaigns receive these sounds after restarting and loading.

Dead knights with negative lives show zero life icons, matching retail. Actual
lives and death rules are unchanged; existing saves receive the correction.

For a campaign, set **Players** to 2, 3 or 4 and choose **Select Knight**.
Player numbers follow selection order, not knight colour. Solo campaign retains
its normal keyboard/controller controls without setup popups.

- Each player chooses a device when first needed, normally at their knight's
  selection turn: **Start** on a controller, or **Enter** for keyboard.
- One player may choose keyboard even when three or four controllers are
  connected. The keyboard is optional and belongs to that player for this session.
- If everyone chooses a different device, assignments follow their knights
  through turns, towns, inventory and duels. Extra controllers stay unassigned.
- When controllers are short, players can choose the same pad during knight
  selection and pass it between turns. Each knight's choice is reused
  automatically; later turns and day changes need no setup Start/Enter press.
  The keyboard stays with its chosen player.
- Duelling humans need distinct devices: two controllers, or controller plus
  keyboard. A player cannot take the keyboard from an inactive third player.
  If both fighters normally share one pad, the other fighter is prompted to
  press **Start** on another controller, or **Enter** to use an unassigned
  keyboard for that fight. Enter is an explicit choice; the game never assigns
  the keyboard automatically. The winner keeps their duel device for looting,
  then their usual shared controller resumes on the map.

| Campaign setup | Choices |
|---|---|
| 2 players, 1 controller | One player chooses controller; the other chooses keyboard |
| 3 players, 2 controllers | Choose two controllers plus keyboard, or share the controllers |
| 4 players, 4 controllers | Choose four controllers, or three controllers plus keyboard |
| 4 players, 2 controllers | Share the controllers; one player may choose to keep the keyboard |

The one-line prompt stays visible until the requested player chooses or confirms
their device. It offers **START OR ENTER** while keyboard is available, then only
the applicable action. Already assigned players confirm without changing devices.

Once everyone has chosen, the sharing mode stays fixed; connecting extra
controllers does not replace a campaign keyboard player or rearrange ownership.
Restarting and loading identifies players
again. Warm F9 keeps current valid assignments and resumes normally.
Missing inactive devices are reclaimed when that player is needed. Losing a
required controller pauses execution. Reconnect it and press Start, or explicitly
claim an unassigned replacement controller. A controller reserved by another
player or sharing group cannot replace the missing pad, even during an idle
turn. The prompt keeps waiting rather than combining the groups. A connection
notification alone does not assign a controller; the requested player presses
Start to identify it. Enter cannot replace a disconnected
normal controller; the temporary keyboard choice is for a shared-pad duel.
When several players share the missing pad, one fresh reconnect claim restores
that shared choice for all of them. The other players keep their assignments.

Keyboard typing remains available for **everyone's custom knight name**, even
when another player owns keyboard gameplay. While a name field is open, text
goes to that knight's name; typing does not change device ownership. Gameplay
keys and mouse actions belong to the keyboard player. Keyboard save/load, pause,
quit and diagnostics remain available.
Tap Start/Enter when identifying a device. The original daybreak screen still
uses its normal Fire acknowledgement, without an additional setup prompt.

Ordinary scene changes use the original game's input handling. There is no
extra release-controls pause, message or timer. Only the Start/Enter press used
for player identification is consumed before play resumes; other held controls
do not block it. Player identification, reconnect and focus-loss prompts remain.

### Practice

Use your preferred controller to navigate the menu or skip the intro. The first
fresh controller input selects the main pad for this run; connection/enumeration
order does not select it. Other pads cannot take over while it stays connected.
After it disconnects, release and use the desired replacement pad. Keyboard and
mouse controls remain available in solo play.

Select **Practice** directly. It always has two fighters, independently of the
campaign's **Players** setting. **P1 keeps the menu controller**, with no extra
Start press or assignment popup. P2 automatically gets the next available
controller from the game's device list, or the keyboard if no other pad is
connected. Extra controllers do not add another setup prompt.
P1's controller also owns the menu when Practice ends.

| Devices | Practice controls |
|---|---|
| Keyboard only | Blue knight; green stays idle, no assignment popup |
| One controller | P1 controller, P2 keyboard |
| Two controllers | P1 menu controller, P2 other controller; automatic |
| Three or more controllers | P1 menu controller, P2 first remaining controller; automatic |

P1 is the blue Practice knight; P2 is green. Both players
use the original movement, attack, damage and result routines. The usual binding
profile applies to each pad. Start identification is fixed in the setup overlay.
In multiplayer, mouse attack input follows the keyboard's owner. When both
players have pads, keyboard and mouse gameplay input are ignored; keyboard
pause/save/load/quit/diagnostic shortcuts remain available.

In multiplayer, assignments do not change when SDL device indices change. A missing device pauses
the game and asks the affected player to reconnect and press Start on their
controller or an unassigned replacement. Keyboard fallback is not offered.
Connecting the first controller during keyboard-only Practice opens P1 setup.
Adding controllers during a round keeps the current assignments. To use a newly
connected controller instead of P2's keyboard, return to the menu and start
Practice again.
Extra unclaimed pads cannot operate the fighters. Closing the window or Esc works
while waiting. Window focus loss pauses execution until focus returns.
The popup shows only the current action; there is no assignment list or
confirmation toast during play. Solo campaign and keyboard-only Practice have no
assignment, recovery or focus popup.

If no menu controller has been identified, such as loading Practice immediately
after restarting, **PLAYER 1 PRESS START** identifies P1 once and assigns P2
automatically. A known menu controller is reused when loading Practice too.
A normal F9 keeps current multiplayer assignments and resumes normally.
Controller identities are not saved. All Practice saves,
including older ones made with campaign Players 1, support both fighters using
the connected devices. Loading a menu or campaign clears Practice assignment.

## Original code used

The original decoder/selector at shipped `0x22fc4` (retail reference `0x22f70`)
chooses D0 for an actor whose selector byte at `+0x0b` is 1, and D1 otherwise.
Practice initializes the green knight with selector 1 and the blue knight with
selector 2. The host supplies independent five-bit action words at that guarded
boundary; the original instructions select and process them. Mouse counters do
not become P2 directions. No new combat implementation or AI behavior is added.

The original Practice setup call at shipped `0x24d36` (retail `0x24e12`) marks the
host setup boundary. The original menu poll clears it. Cold saves are recognized
using opcode, original menu/mode, scene and actor-selector guards. Device IDs,
assignment prompts and input edges are kept outside the save format. Version4
stores the transient rat-kill guard alongside version3's sword-generation
history. Versions2/3 remain readable and start with a neutral missing KO guard;
only new saves can retain that guard exactly in the middle of the guest routine.
No old sword-inventory migration is performed. New saves need this build or newer.
The retail source comparison verifies these boundaries; it is not a claim
that the entire native retail installation has been playtested.
The original setup at `0x210d8` saves the campaign's Players value at shipped
`0x2e064` (retail `0x2de3c`), then sets up two Practice players at `0x210ee`.
The backup is for restoring the campaign setting on exit; it does not restrict
Practice input. The port leaves these original instructions intact.

Campaign uses the original roster at shipped `0x2e7dc` (retail `0x2e5b4`),
stride `0x84`. The colour at `+0x36` does not determine the player number.
Selection follows the original record pointer; map input follows the original
current actor. Combat uses the original scene check before reading a second
participant: monster constructors leave that slot stale. It routes each actual
human actor through that same guarded selector. The original encounter routine
swaps the winner into the first participant slot before opening loot; the host
uses that owner for the pointer and confirmations, including selector-1 winners.
Cold loads reconstruct the context from original state and live call frames.

## Verification

Automated coverage includes both knights' directions/attacks/damage, simultaneous
inputs, release and reload, and retail-parity on/off; SDL virtual pads exercise
identical devices, reversed claims, extra devices, held Start/trigger, focus loss,
both disconnect orders, rejection of keyboard fallback and all-device loss. The live probe
checks that guest instruction/frame counters stop while waiting, audio device
playback resumes, warm F9 preserves ownership and the original pause still works.
The menu probe starts with two controllers already connected and verifies that
the second-enumerated pad can navigate the original menu, without the other pad
stealing control. Device checks include held/background input and registry reuse.
Live checks also enter Practice from the original Players 1/2 menu choices,
verify automatic menu-owner assignment and both original fighters without Start,
and check one/two/four pads, keyboard-only training, pause/save/load,
first-controller hotplug and solo campaign without assignment UI. Cold loads
with no menu owner still test P1 identification; P2 is assigned automatically
even with extra pads, and held gameplay input does not block play. Additional checks cover
24 campaign scene/input combinations with held controls, plus actual scene
transitions, focus return and quickload without the former release gate.
See [the regression instructions](regress/README.md).

The operator has playtested Practice and campaign multiplayer, including shared
controllers, keyboard use, scene changes and reconnection. Automated campaign checks
exercise reversed colour selection, original turn rotation and daybreak, shared
and personal devices, both duel roles, lethal attacks, winner-only looting,
cold/warm loads and controller recovery. The operator accepted manual coverage
as complete on September28, including automated verification of the final map-
input correction. This does not claim every possible hardware layout was tested.
Mixed-device checks also verify custom-name text in the original name buffers
for every player, all four keyboard-player choices, rejected Enter during
reconnect, unused controllers, and one visible claim per simultaneous Start/Enter.
