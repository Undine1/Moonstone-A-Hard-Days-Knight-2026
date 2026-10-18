# Regression check

A tripwire so fixes that quietly come undone are caught here in seconds, not mid-playthrough.

## Build profiles and release gate

`python recomp/build.py` is the canonical recipe (`build.sh` calls it).
It explicitly enables developer tools for the regression engine in `build/`.
`python recomp/package.py NEW_FOLDER` always compiles the stripped public engine
in `build/release/` and enforces `release_gate.py` before and after packaging.
Never publish the default development engine. See [BUILDING.md](../BUILDING.md).

Build a test source with `python recomp/build.py NAME --probe recomp/regress/NAME.c`.
Existing manual compiler commands must define `MOON_DEVELOPER=1`; the source now
defaults to release. Tests are never package inputs.

`check_build_profiles.py --output NEW_FOLDER [--baseline-probe OLD_PROBE]`
compares eight combat/map/town/boot replays across the development/public
`build_profile_probe` builds, including every frame/PCM sample, full save bytes,
cross-profile loads and invalid-load atomicity. It also tests actual public CLI
rejection and rejects stale provenance, a swapped dev engine and a new debug
switch; `--package FOLDER` additionally checks the package allowlist.

`save_profile_probe.c`, `fullscreen_probe.c` and `host_error_probe.c` can be built
with `--profile release` to test real public paths without exposing test switches
in the actual public engine. The first two runners take `--public` (save tests
also take `--build recomp/build/release`). Fullscreen now includes F10/F12:
recorders must work in development and produce no files in release. The public
startup probe captures dialog text and stops after initialization; run the data
suite against it with `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy`.

The original 13/13 golden suite still uses the DEVELOPMENT engine below.

```sh
bash recomp/build.sh                 # build the engine
python recomp/regress/check.py       # run all goldens -> PASS / FAIL
```

Exit code is non-zero if anything regressed (usable in CI / a pre-push hook).

Console-free startup/dialog checks and the audit of former console users are
documented in [console-audit.md](console-audit.md).

`python recomp/regress/check_data_setup.py` checks first-run disk extraction,
missing and invalid disks/modules, filesystem and file-chain diagnostics, read
denials, and actual folder write denials in isolated scratch directories.
Pass `--host-probe-exe recomp/build/host_error_probe.exe` to also verify partial
writes and close failures. The Windows permission tests restore their temporary
directory ACLs before cleanup; source disks and installed files are read only.

The data suite also checks root/header/data/extension checksums on every disk,
with and without cached modules, full-size blank images, and truncated, altered
or oversized cached files. `--retail-data "C:/Users/Ins/Desktop/ipf disk/extracted"`
adds fresh/cached retail checks; retail has no optional Crystal crack intro.
With the host probe and retail data, all58 cases must pass.

Build `loader_integrity_probe.c` with the same compiler/source/library recipe
as `host_error_probe.c` (substitute its filename; keep SDL2.dll beside it).
`python recomp/regress/check_loader_integrity.py` runs21 exact-read/skip,
EOF, changed-file and saved-stream checks against the actual runtime helpers.
It creates all fixtures in a private temporary directory. Never deploy probes.
Pass `--loader-probe-exe recomp/build/loader_integrity_probe.exe` to
`check_startup_errors.py` for a real stream-error dialog owned by the game window
and a headless failure with a nonzero exit. With both probes, all23 dialog and
console-free diagnostic cases must pass. Startup checksum/cache errors include
the affected path and actual log location. Error cases must preserve user files;
clean before/after RAM, PCM, frames and save bytes must remain identical.

`python recomp/regress/check_choke_death.py` replays the frozen canopy choke
through player death, attempts to stab after death, and save/reload. It also
checks that living escape behavior and death-to-inventory timing are unchanged.
Pass `--output recomp/build/choke-review` to retain frame dumps for visual review.

`python recomp/regress/check_practice_input.py` boots into Practice and checks
movement, attack, released controls, damage, and save/reload for the second human
knight. Disabled-fix comparisons must reproduce the former input leakage.

## Guardian reward map transition

`check_guardian_return.py --before OLD_EXE --fixture REWARD_WAIT_SAVE --data DATA
--output SCRATCH [--control OTHER_SAVE ...]` compares the current development
engine with a frozen pre-fix engine. Five dismissal timings must remove the
corrupt pre-tween picture while preserving complete RAM, saves, audio and all
other pictures. Raw capture, waiting and supplied unrelated scenes must agree.
Keep SDL2.dll beside the frozen executable. Fixtures/evidence are private.

Build `guardian_return_probe.c` in both profiles with `build.py --probe`.
It drives the real SDL loop with `--fixture SAVE --output SCRATCH`, plus normal
`--os --sdl --dataset DATA --diskdir DATA --log ABSOLUTE_SCRATCH_LOG` arguments.
Do not pass the developer-only `--mod` argument to the public-profile probe.
Add `--full` for fullscreen, or `--reload 3 OTHER_SAVE` / `--reload 2 OTHER_SAVE`
to test clearing the redraw/tween blackout on load. Compare `frames.txt` and
`final.sav` across profiles and window modes. Never deploy this test executable.
See `notes-handoff/moonstone-guardian-transition.md` for evidence and limits.

## Axe-Trogg recoil movement

`trogg_movement_probe.c` runs the original earlier/retail movement and animation
routines. It compares the correction with native standing-pose bounds, including
two enemies, both facings, all directions/walk phases, scenery, arena edges and
wrapped coordinates. It checks complete CPU/RAM agreement with that oracle,
preserved body obstruction, no newly blocked directions and all four knight
records. Build it in both development and release profiles; never deploy probes.

`trogg_movement_fight_probe.c` and `check_trogg_movement.py` replay the original
saved fights against a frozen prior engine. The suite checks actual contact to
movement timing, unchanged other monsters, damage/animation/audio controls,
and cold/warm loads of old and newly written mid-recoil saves. All logs and
generated saves go to `--output`; existing specimens are read-only.

```powershell
python recomp/build.py trogg_movement_probe --probe recomp/regress/trogg_movement_probe.c
python recomp/build.py trogg_movement_fight_probe --probe recomp/regress/trogg_movement_fight_probe.c
python recomp/regress/check_trogg_movement.py --observations recomp/build/hit-delay-full-20261017 --native-images recomp/build/trogg-recoil-fix-20261017/native --before-exe recomp/build/trogg-recoil-fix-20261017/before.exe --output recomp/build/trogg-movement-check
```

The observation/native images are private fixtures, not distributable game data.
Their preparation and the actual SDL keyboard/controller checks are recorded in
`notes-handoff/moonstone-hit-recovery.md`. The full 13/13 suite is also required.

## Inventory stat numbers

`stat_display_probe.c` and `check_stat_display.py` verify the inherited stale-
text-width bug (CON1 could print as257). Build the probe with the production
compiler recipe, substituting its source and output name; never deploy it.
Pass `--images` a local directory containing the original `port.ram` and
`retail.ram`, `--before-exe` a frozen prior engine, and `--output` a scratch
directory. The controlled inventory entry defaults to the existing local
`recomp/build/item-action-20260923/checked-entry.sav` fixture (`--entry` overrides).

The suite checks1,440 native original/fixed cases,18 scope/flags guards,32 panels
across all four knights, both knight-loot columns, eight cold/warm save replays
and a real CON purchase/redraw. It preserves actual stats, items, XP and HP;
`--nostatdisplayfix` must reproduce the prior engine's complete RAM. This is a
display bug in both originals, independent of `--noretailparity`. The full13/13
regression suite is also required. Fixtures, images and private EXEs stay local.

## Manual armour loot

`check_manual_armour.py` executes the retail transfer/stat instructions as an
oracle for all 16 armour pairs, with four health/CON/ring profiles (64 cases).
Sound and drawing alone are stubbed for these isolated comparisons. Another
96 cases click and redraw the real campaign loot UI without stubs, covering
alive/dead losers, one-pick limits, repeated clicks, health and unrelated items.
It also checks save/reload, the disabled-fix crash/HP defects, sound disabled,
invalid gear, and optional byte equality against a frozen pre-fix executable
for all 16 automatic armour/gold outcomes and parity-off controls.

Local fixtures are copyrighted and must not be committed or changed. The
campaign fixture is a full save before the loot screen is composed (winner
`0x2e7dc`, loser `0x2e8e4`), generated by the campaign-duel probe below. Example
using the frozen local specimens:

```powershell
python recomp/regress/check_manual_armour.py --fixture recomp/build/mixed-warm-duels/campaign-3p-2pads-kb3-duel-0.sav.loot.sav --retail "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --before-exe recomp/build/manual-armour-20260921/before-moonstone.exe --output recomp/build/manual-armour-check
```

`--nomanualarmorfix` restores only the former manual armour routine for A/B.
All output is scratch; existing saves and installed logs remain untouched.

## Sword generation, weapon loot and saves

`check_special_sword.py` compares complete world initialization and repeated
item grants with original retail instructions, including all recipient modes.
It verifies all15 legal weapon pairings, lair/hoard/acquisition transfers, shop
prices and ownership, 45 real duel-loot UI cases, actual temple buying/selling,
repeat clicks and A/B reproduction of the old failures. The generation latch
is retained across separate processes using actual serialized saves.

```powershell
python recomp/regress/check_special_sword.py --fixture recomp/build/mixed-warm-duels/campaign-3p-2pads-kb3-duel-0.sav.loot.sav --retail "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --output recomp/build/sword-check
```

Optional `--before-exe` compares the focused `--noswordfix` failures with a
frozen earlier executable. No original specimen, data or installed log changes.

Compile `regress/sword_state_probe.c` with the probe build command below,
substituting it for `regress/multiplayer_probe.c` and choosing output
`build/sword_state_probe.exe`. Then run from the repository root:

```powershell
recomp/build/sword_state_probe.exe --os --mod dist/MoonstoneNative/data/nb --dataset dist/MoonstoneNative/data --diskdir dist/MoonstoneNative/data --frames 3 --loadstate recomp/build/sword-check/flag-one.sav --sword-state-dir recomp/build/sword-check --log recomp/build/sword-check/state-probe.log
```

This test-only executable exercises warm zero/one latch restores and the next
original item grant, a save/load roundtrip, and atomic rejection of an invalid
latch, truncated/trailing data, missing file and invalid streamer. Never ship it.

## Dragon foot-strike damage

`check_dragon_damage.py` executes both games' original combat-table initializers
and hit dispatchers for 208 armour, talisman, attack and HP boundary cases.
It checks entry saves containing either opcode, the isolated A/B switch and
full parity-off behavior. Seven actual saves taken during the damage routine
resume to identical guest RAM. The test-only `dragon_damage_probe.c` includes
the current host and checks warm loads, a real save roundtrip, all 32 incoming
condition-code combinations against retail's native MOVE (including cycles),
and mismatched-code/lineage guards. Build it like the other probes; never ship it.

```powershell
python recomp/regress/check_dragon_damage.py --retail "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --output recomp/build/dragon-check
```

`--nodragondamagefix` restores only the earlier foot-strike calculation while
keeping the minimum-five floor. `--noretailparity` disables both retail changes.
The entry hook recognizes and normalizes either saved opcode, so the A/B switch
also works with a save written after the corrected attack. Save format is unchanged.
The full 13 regression baselines remain unchanged by this fix.

## AI-knight defensive reactions

`check_knight_reactions.py` compares 288 cases (576 original-code executions)
against retail: actual knight/table initialization, both defensive choices,
all eight thresholds, facing/distance/RNG boundaries, repeated block states,
mismatched blocks and dead actors. Other actor flags and the low state byte
must remain intact. Optional `--before <exe>` checks that the isolated
`--noknightreactionfix` rollback exactly matches a frozen pre-change build.

Build test-only `knight_reaction_probe.c` like the other probes, then run:

```powershell
python recomp/regress/check_knight_reactions.py --retail "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --output recomp/build/knight-reaction-check
```

The probe compares BCLR flags against the native retail instruction for all32
CCR values and seven actor-flag patterns. It checks repeated warm loads, real
save roundtrips at the old gate's intermediate PC, five consecutive contacts,
and28 scope/code/lineage guards. Every run requires a scratch `--log`; never
ship the probe. The fix changes no save format and all13 goldens stay unchanged.
Natural fight captures, independent native-instruction controls, human death
and canopy frame comparisons are in `build/ai-knight-20260921/`.

## Once-per-lair XP

Build test-only `regress/lair_xp_probe.c` like the other probes, then run:

```powershell
python recomp/regress/check_lair_xp.py --retail "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --output recomp/build/lair-xp-check
```

The probe executes 576 reward comparisons against retail, covering all24 lairs,
four players, first/repeat wins, XP word boundaries and all32 condition codes.
It verifies the shared reward across players, all24 treasure slots and gold,
empty-lair closure, defeats, new-world resets, pointer/code guards and actual
save/load atomicity. The production EXE independently exercises twelve cold
restarts, different lairs and saves at every award instruction boundary.
`--before <exe>` also checks exact guest-RAM agreement with `--nolairxpfix`.

Save format v5 adds24 validated bytes after the sword/rat guards. V2/v3/v4
remain readable with missing lair history cleared. The sword and rat suites
understand the new offsets. A real combat -> leave loot -> map -> next day ->
reenter -> combat -> loot replay is preserved in `build/lair-xp-20260921/revisit/`.
Its second win leaves XP1; A/B leaves XP2 and changes exactly that guest-RAM byte.
This ratman fixture returns with two defenders, not the user's remembered one;
do not claim the exact single-defender case was reproduced. Never ship a probe.

## Rat killing sequence and save lifecycle

`check_knockout.py` compares 96 lethal/nonlethal, facing, simultaneous-contact
and gore combinations with the original retail human handler, rat-damage
dispatcher, animation installer and round-end helpers. Eight additional cases
exercise both rat reactions with a clear/set latch and living/dead contact.
It also verifies the reset boundary and inactive records from recycled actors.

Build `regress/knockout_probe.c` with the probe command below, using output
`build/knockout_probe.exe`, then run:

```powershell
python recomp/regress/check_knockout.py --retail "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --output recomp/build/knockout-check
```

Five saves taken inside the original helper chain must resume to identical
guest RAM. Actual warm loads verify both latch values, neutral legacy v2/v3
loads, a real save roundtrip and atomic rejection of malformed files and both
sidecars. The probe explicitly enables assertions even in optimized builds.
`--nokofix` restores the previous incomplete parity implementation for A/B;
`--noretailparity` still restores the earlier engine behavior.

The September21 combat golden changed by seven explained RAM bytes: retail's
no-new-animation sentinel and an inactive display workspace no longer cleared
by a stale-owner lookup. Every one of 600 captured frames, life loss and the
frame363 inventory transition remained identical. The local full-play scripts,
both knight-death animations and visual evidence are retained under
`recomp/build/ko-fix-20260921/`; see the handoff's `moonstone-knockout.md`.

## Two-player Practice preview

The original-core replay creates its own Practice fixture from the local disks:

```powershell
python recomp/regress/check_multiplayer_practice.py --output recomp/build/multiplayer-check
```

`--script2` is an opt-in headless diagnostic input channel for P2; without it,
existing headless scripts retain their single-player semantics. Replays check
both fighters independently, simultaneous movement, attacks/damage, release,
save/reload and the retail-parity off path.

Build the test-only SDL virtual-controller probe from `recomp/`:

```powershell
$compiler = './tools/zig-x86_64-windows-0.16.0/zig.exe'
$probeSources = @(
  'regress/multiplayer_probe.c', 'src/loader.c',
  'vendor/Musashi-master/m68kcpu.c', 'vendor/Musashi-master/m68kops.c',
  'vendor/Musashi-master/m68kdasm.c', 'vendor/Musashi-master/softfloat/softfloat.c'
)
& $compiler cc -DMOON_DEVELOPER=1 -O2 -g -std=c11 `
  -Ivendor/Musashi-master -Ivendor/Musashi-master/softfloat -Isrc -Ivendor/SDL2/include `
  -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unused-function -Wno-date-time `
  @probeSources vendor/SDL2/lib/libSDL2.dll.a -o build/multiplayer_probe.exe
```

Then, from the repository root:

```powershell
python recomp/regress/check_multiplayer_host.py --fixture recomp/build/multiplayer-check/practice.sav --output recomp/build/multiplayer-host-check
```

The runner copies the probe and default controls into a temporary installation,
so F5/F9 operate only on its scratch save. It briefly opens a game window, uses
actual SDL virtual game controllers, and captures the overlay. It tests input
ownership, hotplug/recovery, background claims, held controls, quickload, failure
to load a nonexistent save, guest freeze, audio-device resume and original pause.
It also boots a disposable main-menu save and checks that the second-enumerated
controller can navigate the original menu while other pads cannot steal it.
Device tests cover held/background input, registry slot reuse, replacement after
disconnect and Practice P1 retaining menu control without replaying held shortcuts.
The replay fixture leaves campaign Players at its default of 1. Live probes enter
Practice from campaign Players 1 and 2 without pressing Start or showing an
assignment prompt: the menu controller becomes P1 and the first remaining pad
or keyboard becomes P2. Five actual menu-entry cases cover one, two and four
pads, reversed menu ownership, both original fighter readers, warm F9 and
loading Practice from a menu with a known owner. Ten device cases cover every
possible menu owner with one through four pads, including held P2 input.
Cold loads without a known owner ask only P1, even with extra pads. Recovery
still requires an explicit claim; spare hotplug never replaces keyboard P2 or
an assigned pad. Both disconnect orders and all-device loss remain covered.
Keyboard-only Practice movement, original pause and save/load have no assignment
UI; connecting the first pad opens setup. Solo campaign with two pads still has
no assignment popup. A warm load of the menu must clear Practice setup. Generate
the current default-campaign Practice fixture with the replay command above.
Sep24 menu-owner inheritance evidence and protected deployment are recorded in
`build/practice-menu-owner-20260924/` and
`notes-handoff/moonstone-practice-menu-owner.md`.
Scene-input checks cover24 combinations of actual campaign context changes and
held buttons/sticks/triggers/keyboard/Enter/mouse. Assigned scenes remain in
PLAY, accept held input and have no release message. The live Practice check
verifies that P2's held attack no longer blocks play after P1's identification,
and that focus return and F9 resume with held directions. Identification still
consumes only its accepted Start/Enter press, including automatic P2 assignment.
Campaign tests inspect actual pixels and phases on selection/map/daybreak/duel/
loot boundaries while retaining persistent player-identification, input
ownership, freeze/reconnect and original pause checks. No test clock is needed.

The probe exposes only its SDL virtual controllers to the tested registry;
physical controllers can stay connected. Never package this probe. Real-pad
playtesting remains necessary.

## Separate save profiles

The save launchers use only Windows APIs, with an explicit entry point and no
C runtime. `build.sh` keeps those flags separate from the engine's normal build.
To check launcher paths, verbatim argument forwarding, errors and length bounds:

```powershell
python recomp/regress/check_launchers.py --output recomp/build/launcher-check
```

Use `--launchers PATH` to select another build and optionally `--previous PATH`
to compare an older pair of launchers. This runner builds test-only observers
with the existing Zig compiler, exercises both actual launchers from moved,
Unicode and long folder names, and requires a new scratch output directory.
It never starts the game or opens personal saves/logs. Native source tests
cover missing/invalid children, errors, drive/UNC paths, handle cleanup and
command/buffer limits; error dialogs are captured without user interaction.
Run the profile checks below as well to verify actual engine F5/F9 behavior.

Build `regress/save_profile_probe.c` using the same source list and compiler
flags as `multiplayer_probe.c` above, replacing that probe's source/output
names. `build.sh` also builds the two GUI launchers from `src/save_launcher.c`.
Keep the probe out of every distributed package.

```powershell
python recomp/regress/check_save_profiles.py --output recomp/build/save-profile-check --other-fixture PATH_TO_COPIED_MULTIPLAYER_SAVE
python recomp/regress/check_multiplayer_host.py --fixture recomp/build/multiplayer-check/practice.sav --output recomp/build/profile-host-check --save-profile multiplayer
```

The profile runner requires a new output directory. It copies the diagnostic
engine, DLL and controls into private installs and supplies absolute scratch
logs on every execution. Default/explicit solo and multiplayer choices exercise
real SDL F5/F9, a manually renamed old campaign, missing/corrupt saves, and a
locked destination that prevents atomic replacement. It also checks creation
of `saves/Singleplayer` and `saves/Multiplayer`, missing folders on F9, files
blocking either directory, and real Windows ACL denial of folder creation.
The temporary deny ACE affects only a new private test folder and is removed
in a finally block. Other slots and root/legacy saves must remain unchanged;
failed loads must preserve the running state and
failed writes the prior save. Round trips reload another campaign and return
without changing the session's save choice. The two actual launchers are moved
to a path with spaces and tested from a different working directory. The runner
also checks explicit diagnostic paths, identical RAM/audio/save bytes between
profiles, invalid selectors and GUI subsystem flags. Save format stays v5.

`--save-profile` on the existing multiplayer host runner tests Practice entry,
controller recovery and warm quickload against either filename. The profile
never chooses the game's campaign Players setting or supplies device ownership.

## Campaign controllers

The same probe includes `campaign_probe.h`. After building it and running the
host checks above (which create `menu.sav`), run:

```powershell
python recomp/regress/check_multiplayer_campaign.py --menu recomp/build/multiplayer-host-check/menu.sav --output recomp/build/campaign-check
python recomp/regress/check_multiplayer_campaign.py --menu recomp/build/multiplayer-host-check/menu.sav --output recomp/build/campaign-duels --duel-fixture recomp/build/campaign-check/campaign-4p-2pads.sav --case 4:2,4:1,4:4
python recomp/regress/check_multiplayer_campaign.py --menu recomp/build/multiplayer-host-check/menu.sav --output recomp/build/campaign-recovery --duel-fixture recomp/build/campaign-duels/campaign-4p-2pads-duel-1.sav --restore-context 4 --case 4:2,4:4
```

Setup cases drive the original menu and select colours in reverse order, type
names, then end each original human turn through a complete day. The default
matrix covers 2 players / 1 or 2 pads, 3 / 3, and 4 / 1, 2 or 4. Saves, logs,
settings and screenshots are scratch; physical devices are excluded by the probe.
Personal-device selection handoffs also check the actual rendered prompt pixels
over repeated frozen frames, reject the wrong device and attack input, survive
focus loss, and wait for Start/Enter to be released before selection resumes.
The SDL device tests cover held/background Start and device loss during confirmation,
all four possible campaign keyboard owners with extra pads, rejected Enter during
controller recovery, hotplug without replacing keyboard, and simultaneous claims.

Use `--case 3:2:3,4:4:1` to choose keyboard for P3 in a three-player/two-pad game
and P1 in a four-player/four-pad game. The optional third field lists one-based
players joined with `+`: `3:1:1+2` shares the keyboard between P1/P2, and
`4:0:1+2+3+4` starts with keyboard only. For keyboard-only duels the test waits at
the connect-controller prompt, warm-loads it, then connects a virtual controller.
`--shared-controller` makes non-keyboard players choose the same pad even when
spares are connected. These shared-device duels finish loot and verify every
player's normal choice on their next map turn. Omitting the third field prefers
available controllers. All players' typed custom names are
asserted in the original name buffers. Add `--duel-after-setup` to retain actual
device choices through selection, a full turn rotation, both original duel roles,
warm F9, lethal attacks and winner-owned loot. This distinguishes a warm session
from `--duel-fixture`, which intentionally identifies players again on cold load.

For explicit temporary keyboard use, add `--duel-keyboard --duel-opponent 2`
to `--duel-after-setup --case 3:2,4:2`. P1/P2 choose the same controller during
actual knight selection, and P3 chooses the second. The driver checks the
Start/Enter prompt, uses Return or Numpad Enter, retains normal choices across
warm pending/combat/loot saves, exits the original loot menu, and confirms the
shared controller resumes on the map. Both attacking/defending roles run, so
the keyboard player both wins and loses. Use `--case 3:1:3,4:1:3,4:1:4` with
the same duel options to test borrowing P3/P4's keyboard with only one controller.
These cases also return through the original turn sequence until the keyboard
owner plays again. The host suite covers all seven three/four-player keyboard
owner positions in both duel roles, focus, loot, AI return, and a pad disconnect
during the loan. Explicit initial keyboard sharing is allowed; keyboards needed by a current
fighter (including missing active bindings), and disconnected-pad recovery
remain protected. The host suite also checks background/held Enter and input isolation; the
campaign ownership matrix covers temporary keyboard duels across all slots.
It also checks all336 keyboard/three-pad layouts across2–4 players,3,744 turns
and3,488 duels, including both loot outcomes, all next owners and temporary-pad
loss. The SDL suite exercises31 shared-keyboard/controller layouts with zero
or four connected controllers, including held/background identification.

Duel cases arrange an encounter with the original active-actor preconditions,
then call the original encounter handler. They check both combat ports,
simultaneous movement, warm load, original lethal attacks and winner-only loot
for both attacking and defending P1. `--restore-context 4` checks cold combat
loads, focus freeze, required-controller replacement and retained other devices.
Use context `3` with a generated `.sav.loot.sav` for loot, or context `1` with
`.sav.selection.sav` for character selection. Context `0` verifies a menu save
stays out of multiplayer setup, even with Players 4 and Select Knight highlighted.
Return-to-menu loads must discard campaign setup. These checks supplement the
full 13-row harness and Practice checks; they do not replace real-controller play.

## What it checks

Menu feedback has a native sound/save probe and full UI/PCM checks:

```powershell
python recomp/regress/check_menu_sfx.py --before-exe recomp/build/menu-sound-fix-20260923/baseline.exe --output recomp/build/menu-sfx-check
```

Build test-only `menu_sfx_probe.c` with the same Musashi/loader/SDL inputs as
`multiplayer_probe.c`, producing `recomp/build/menu_sfx_probe.exe`. Never deploy
the probe. The frozen pre-sound-fix executable is required for transaction and
disabled-mode comparisons. `--fixtures` defaults to the read-only September23
investigation fixtures; their provenance and the retail28-call oracle are in
`notes-handoff/moonstone-menu-sounds.md`. The script also reads the earlier
documented armour/sword/UI fixtures under `recomp/build/`.

Checks cover24 UI cases, actual recorded PCM, five production/probe full-RAM
comparisons, ten rejected actions, repeated loot/purchases, both A/B switches,
all25 injected sites with32 CCR values, every native-call save boundary on warm
and cold load, and unchanged save v5. The two town parity goldens intentionally
changed only in native audio state when the missing city Exit click was restored;
340 paired frame captures match. Every invocation has a scratch `--log`.

AI XP has an additional original-instruction oracle:

```
python recomp/regress/check_ai_xp.py --retail "<retail>/retail_mog.sav" --before "<frozen-pre-XP>/moonstone-before.exe" --output recomp/build/ai-xp-check
```

Build `ai_xp_probe.c` with the same source/link inputs as `lair_xp_probe.c`;
never deploy a probe. Tests cover native retail ADDQ/ADDI flags, twelve-day
cadence matrices, complete random gifts, both original stat-price selectors,
AI victory loot, actual token restrictions, human XP versus token writes and
seven warm/cold save boundaries. Every invocation redirects its log.
The `--before` argument is optional and must be a build from before the AI XP
correction. The natural `townarmorP` replay explains its sole changed byte:
`0x2e8af`, AI XP 0->1 after victory. Other goldens are unchanged.

- **`ramfnv`** — cold-boots the engine to a fixed frame and FNV-1a hashes all of RAM.
  Catches *any* unintended change to engine/game behaviour (a stray write, a logic change).
- **`framesha`** — loads a save, renders a fixed frame, SHA-256s the pixels. Catches
  *display* regressions (e.g. an outro/ending screen rendering garbled).

Goldens live in [`manifest.tsv`](manifest.tsv) — only the **hashes** are committed. The
saves and `.adf` game data they reference are local copyrighted content and stay out of
the repo. The engine log is redirected to a temp dir, so your `moonstone.log` is untouched.

## Stonehenge offerings

Build test-only `danu_probe.c` with the normal Musashi/loader/SDL inputs as
`recomp/build/danu_probe.exe`. Never deploy it. These commands require the
read-only original-module and UI fixtures documented in
`notes-handoff/moonstone-danu-offerings.md`; no disks/saves belong in Git.

```powershell
python recomp/regress/check_danu.py --before-exe recomp/build/danu-fix-20260923/before-moonstone.exe --output recomp/build/danu-check
python recomp/regress/check_danu_edges.py --checks recomp/build/danu-check --before-exe recomp/build/danu-fix-20260923/before-moonstone.exe --output recomp/build/danu-edges
```

The first script checks 2,432 original/current native cases, exact retail results
except the intentional labelled ring/talisman offers, actual UI/ritual/reward,
four inventories, XP purchases, cap five, empty/invalid icons, both disable flags
and 153 cold transaction/sound/ritual boundaries. The second checks inactive
scenes/lineages/layouts, repeated cached clicks, 24 warm reload boundaries after
a completed offer, 12 old invalid transaction boundaries and all four matching
Moonstone deliveries against frozen full RAM/PCM. Every run uses a scratch log.
Old already-committed corruption cannot be safely reconstructed and is not
silently repaired. Original-module runs stub presentation only; UI/save/quest
runs execute the complete engine. Retail reference is original native code,
not a recording of retail GUI playback.

## Combat display effect and callback registration

Build test-only `task_effect_probe.c` with the normal Musashi/loader/SDL inputs
as `recomp/build/task_effect_probe.exe`. Never deploy it. Local reference
fixtures are documented in `notes-handoff/moonstone-task-timing.md`.

```powershell
python recomp/regress/check_task_effects.py --before-exe recomp/build/effect-fix-20260924/before.exe --output recomp/build/effect-check
```

The frozen pre-fix executable is required for the four combat replay and image
comparisons; `--native-only` runs just the original-code/interrupt/renderer suite.
The native suite compares all 36 overlap phases and return registers to retail,
injects actual level-3 interrupts, checks cursor movement, capacity, inactive
guards and warm restores, and verifies every pixel of a synthetic playfield
and fixed hardware sprite. Real Balok/Troll attacks must retain full gameplay
RAM and display exactly the native offsets for three frames apiece. All 36
cold save boundaries must match continuous play after the effect ends. Every
invocation has a separate scratch log; no installed save is modified.

## Active menu cursor RNG

Build test-only `cursor_rng_probe.c` with the normal Musashi/loader/SDL inputs
as `recomp/build/cursor_rng_probe.exe`; never deploy it. Reference snapshots
are documented in `notes-handoff/moonstone-rng.md`. The frozen prior EXE needs
the existing SDL2.dll beside it, as any isolated copy does.

```powershell
python recomp/regress/check_cursor_rng.py --before-exe recomp/build/cursor-rng-20260924/before.exe --output recomp/build/cursor-rng-check
```

This compares two consecutive cursor ticks with original retail for2,048
seed/CCR/input cases,62 actual interrupt boundaries and56 inactive guards;
checks69 warm/cold native-call saves; and replays real scenes with exact
`--nocursorrng` controls. The new seed is also supplied to the frozen old EXE
to verify later AI/animation differences independently of the implementation.
Only three parity-on golden hashes changed; parity-off and frontend behavior
remain unchanged. Every game invocation has a separate scratch log.

## Dragon-fire death timing

Build test-only `dragon_fire_probe.c` with the normal Musashi/loader/SDL inputs
as `recomp/build/dragon_fire_probe.exe`; never deploy it. Original earlier and
retail fixtures are documented in `notes-handoff/moonstone-animation-data.md`.

```powershell
python recomp/regress/check_dragon_fire.py --output recomp/build/dragon-fire-check
```

The suite compares complete native animation streams for12 HP/facing/gore
configurations, checks isolated rollback,64 native-reader register/CCR/cycle
pairs,43 inactive/signature guards and71 warm plus71 cold saves across the
repeat/countdown instructions. Old loops already in progress must finish their
saved count; future poses use retail timing. Every invocation needs a scratch
log. Full production-engine A/Bs, combat saves, frames/PCM, canopy and multiplayer
results for this change are in `build/dragon-fire-fix-20260924/`. The existing
13 regression goldens remain unchanged. `--nodragonfirefix` isolates this change.

## Initial grassland lair templates

Build test-only `lair_setup_probe.c` with the normal Musashi/loader/SDL inputs
as `recomp/build/lair_setup_probe.exe`; never deploy it. Read-only original
module fixtures are documented in `notes-handoff/moonstone-initial-lairs.md`.

```powershell
python recomp/regress/check_lair_setup.py --output recomp/build/lair-setup-check
```

The suite executes192 native world initializations over12 seeds and1..4 human
players,15,360 native encounter-count calculations over all24 lairs and stat
thresholds,384 reader/CCR pairs,138 guards and81 warm plus81 cold save boundaries.
Retail and literal controls must agree with the fix. Already copied node records
must remain intact when resuming an interrupted initializer. `--nolairsetupfix`
isolates the change; the master `--noretailparity` also restores the earlier
templates at their next copy. Neither switch rewrites existing live encounters.
Production versus frozen-engine RAM/save/PCM/frame controls, real campaign
creation and affected encounter replays are in `build/lair-fix-20260924/`.
Every executable invocation has a scratch log. Save version5 and all13 existing
goldens remain unchanged. Automated combat assistance and incomplete manual-play
coverage are recorded explicitly in the topic note; do not call these full
unassisted playthroughs.

## Version overlay

Build test-only `version_notice_probe.c` with the normal loader/Musashi/SDL
inputs as `build/version_notice_probe.exe`; never deploy it. Supply a solo
campaign save awaiting map input:

```powershell
python recomp/regress/check_version_notice.py --probe-exe recomp/build/version_notice_probe.exe --fixture recomp/build/lair-fix-20260924/human-map.sav --output recomp/build/version-toggle-check
```

The real SDL host loop checks V and a remapped key/controller button, held keys
and repeat events, toggling off, live movement, quicksave/load and inventory
scope. Complete guest RAM/audio are compared with a run that never requests
the overlay; rendered images verify show/hide. All files live in temporary or
explicit scratch directories. Frozen-executable reproduction, old blocked-save
recovery and unrelated native-wait controls are in
`build/version-display-20260924/`. The normal 13 goldens must remain unchanged.

## Multiplayer disk transitions

Build test-only `disk_transition_probe.c` and `transition_sdl_probe.c` with the
normal Musashi/SDL recipe and keep SDL2.dll beside them. Never deploy probes.
The native probe requires absolute scratch `--log`, `--fixture` and `--dataset`
paths; `--checkpoint` checks four interrupted press/release boundaries.
`--retail-image` accepts the verified 20-byte-header + 2MB retail RAM image.
It checks all four owners, AI attackers, held buttons, the settling delay and
ordinary manual waits using the original input/wait instructions.

`check_disk_transitions.py` uses the real SDL loop and virtual devices in an
isolated install. Supply `--probe-exe`, `--map`, `--tower` and `--output`.
The tower fixture must be a two-player campaign on P2's map turn, positioned
at Math (217,11), with native DiskB state. The attack fixture must have valid
native DiskC/cache state: first run `--case prepare` using the tower fixture,
then use its `prepare-after/map-after-math.sav` for `--map`. This follows the
original tower/Exit routines, placing the menu cursor over Exit during setup.
The encounter test calls original `21ab4` with an AI attacker and P2 defender;
it does not fake a disk change. Use `--keyboard-p2` to repeat both transitions
with P2 on the keyboard. No P1 button is pressed during either transition.
Optional `--before-exe` expects the frozen pre-fix diagnostic to reproduce the
hidden wait. Every invocation uses an explicit scratch log.

Evidence and exact fixture provenance: `build/p2-transition-20260924/` and
`notes-handoff/moonstone-disk-transitions.md`. Both scene captures were visually
checked. These are targeted native/SDL tests, not a physical-controller campaign
playthrough. All13 existing goldens must remain unchanged.

## Campaign shared choices and monster participants

Build `multiplayer_probe.c` and test-only `campaign_ownership_probe.c` using the
normal loader/Musashi/SDL recipe. Keep SDL2.dll beside them; never deploy probes.

```powershell
python recomp/regress/check_campaign_ownership.py --menu recomp/build/practice-menu-owner-20260924/host/menu.sav --retail-image "C:/Users/Ins/Desktop/ipf disk/extracted/retail_mog.sav" --output recomp/build/campaign-ownership-check
```

The native probe executes the original second-slot scene gate for582 cases in
each disk lineage, then checks48 device-choice configurations,608 automatic
turns,360 duel combinations and shared reconnect/temporary-pad loss. It also
checks156 two-controller layouts across2..4 players with three repeated losses
each: recovery rejects another group's remembered pad, including an idle pad
without an active owner. Existing shared duels and temporary loans remain valid.
The SDL host suite reproduces the reported wrong-pad Start with3/4 players and
six alternating disconnects each, verifies new connection IDs, held Start,
warm campaign reconstruction, all players' map/fight/UI input and daybreak/P1
ownership. Its new check fails against the frozen Sep27 shipping source.
The SDL
runner performs actual four-player selection/naming/day rotation followed by
original lair loading/combat as P2/P3/P4 with a deliberately stale second knight
pointer. It verifies participant isolation, movement, warm and cold saves.
`--before-probe` additionally preserves the old false-P2 prompt and reloads it.
The general host suite includes shared-group reconnect and temporary-duel-pad
checks with real SDL virtual-controller events. Run the campaign suite with
`--case 4:2 --duel-after-setup --duel-opponent 3` to test two players who normally
share a controller through original lethal combat and winner loot in both roles.
All output is scratch; every executable run has an explicit log.

## Numbered map choices

Build test-only `numbered_menu_probe.c` with the normal loader/Musashi/SDL recipe.
`check_numbered_menu.py --fixture <copied three-choice popup save> --output <scratch>`
checks all three actions for P1-P4 through actual SDL controller and keyboard/
numpad events. It checks inactive devices, held buttons, wraparound, warm loads,
and focus/reconnect recovery at the popup. P1/P2/P4 variants change the active map
actor in scratch only; the P3 case uses the user's unmodified saved screen.

`numbered_menu_probe.exe --loadstate <same copy> --probe-retail <retail_mog.sav>
--log <scratch>` executes352 native cases across both disk lineages and both
loaded copies, with2..9 entries. Controller confirmation matches the original
keyboard parser's complete RAM and selected record/action. Invalid indices,
held opening presses, cold cursor reconstruction and opcode guards are checked.
Never deploy the probe. Frozen shipping source reproduces the old Down->option2
immediate dispatch. Evidence: `build/numbered-menu-20260928/`.

## Map input after an eliminated player

Build test-only `map_input_probe.c` with the normal loader/Musashi/SDL recipe.
Run `check_map_input.py --fixture <copied P3 map save> --retail <retail_mog.sav>
--output <scratch>`. Both inputs are standard MOONSAVE files with RAM at104.
The reported fixture has P2 dead, P3 active and the native dead-human counter1.

The probe compares480 original-retail input paths for P1-P4/AI, dead counts,
modal flags, movement, End Turn and inventory. It also checks the unchanged
counter/no-death paths and reproduces the old failure with parity disabled.
The runner exercises32 SDL controller/keyboard cold/warm cases, with inactive
devices rejected and one native action per press. P3 uses the exact specimen;
other owners use explicitly derived scratch saves. All logs/saves stay in the
output folder; never deploy the probe. Evidence: `build/p3-endturn-20260928/`.

## Fullscreen

Build test-only `fullscreen_probe.c` with the normal build.sh compiler inputs.
Run `check_fullscreen.py --probe-exe <absolute probe.exe> --menu <menu.sav>
--practice <fresh practice.sav> --map <solo map.sav> --output <scratch>`.
Every launch uses a temporary installation and an explicit scratch log.

The 12 real SDL scenarios check F11/Escape and repeats, window size/position,
maximized restoration, aspect ratio, entry/exit failures, remapped Escape,
reserved F11, intro/map/combat, identification and disconnect/reconnect.
Minimize/restore uses Windows; focus events are injected because Windows can
deny automated foreground activation. This is not a physical Alt+Tab test.
Menu runs compare full guest RAM against a windowed control, and returning to
windowed mode must reproduce the same game pixels. Never deploy this probe.

## Inventory exit shortcut

Build test-only `inventory_exit_probe.c` with the normal build.sh compiler
inputs. Run `check_inventory_exit.py --modules <original flat RAM directory>
--fixtures <stat-cap final-checks directory> --output <scratch directory>`.
The module directory must contain the original `port.ram` and `retail.ram`;
fixture names are `s{0,3,10}-p{1..4}-con-allowed-wait.sav`.

The native oracle compares full cleanup against the existing Exit button in
retail and both host-supported lineages, including active cursors and pending
Wyrm targets. Drawing/wait/audio leaves are substituted. The 35 real SDL cases
exercise inventory, offerings and loot for all four owners, solo play, inactive
devices, held buttons/key repeat, focus, warm load, remapping, unbinding and
older controls files. They assert unchanged stats/items at native return.
These are controlled panel tests, not complete scroll casts. All logs/saves
stay under the scratch output; never deploy the probe.

## Inventory scroll transitions

Build test-only `inventory_flow_probe.c` and `inventory_flow_sdl_probe.c` with
the normal build.sh inputs. Run `check_inventory_flow.py --fixtures <original
scroll-review directory> --output <scratch>`. The private fixture directory
contains original `port.ram`/`retail.ram`, `p{1..4}-inventory.sav` and the old
scroll captures documented in `notes-handoff/moonstone-scroll-return-review.md`.
Use `--only native|fresh|legacy|boundaries` for a focused run; `--only pending`
checks saved transitions with A/B options changed after the boundary run.

The native oracle compares complete Acquisition/Wyrm casts, repeated casts,
knight-loot transitions, caller stack depth and all four owners with retail.
It includes the old behavior as an A/B control and validates instruction/stack
guards and CPU flags. SDL tests exercise real drawing/input, item consumption,
transfers, dragon targets, keyboard/controller ownership, single cleanup,
existing nested saves, and warm/cold saves during entry/re-entry/exit.
Fixtures use a controlled caller; these are not full campaign playthroughs.
Every run uses copied saves and an explicit scratch log. Never deploy probes.

## Boot splash storage and rendering

Build the test-only `splash_probe.c` with `build.py splash_probe --probe
regress/splash_probe.c --profile release`. Run `check_splash.py --probe
<absolute probe.exe> --output <new scratch directory>`. It compares the actual
SDL texture upload with `icon/splash.png` and checks fullscreen/windowed return
pixels. `--baseline-probe <old probe.exe>` additionally requires all three
captures to match a probe built before the change. `--baseline-header <old
header>` independently decodes a previous base64/QOI or raw-pixel header and
verifies every original RGB pixel. The public splash uses raw RGB24, with no
decoder or temporary image allocation; the source PNG and icon art stay intact.
The probe has its own scratch log and never writes a player save. Do not ship it.

## Interrupted knight fall

Build test-only `knight_fall_probe.c` with `build.py knight_fall_probe --probe
regress/knight_fall_probe.c`. Run `check_knight_fall.py --before-exe <frozen
previous engine> --output <scratch>`. Its default read-only specimens are in
`build/trogg-death-review-20261017/`; see the death-animation topic note.
`--resume-saves` runs only the saved-boundary continuation checks.

The native probe compares the fix against original pause expiry, including both
editions/facings, four knights, repeat hits, sound calls, full RAM/CPU and scope
guards. Full replays compare normal deaths, paired rat kills, knight decapitation
and canopy escape/death; six instruction-boundary saves get cold/warm restores.
The latter requires identical full saves/pictures and all PCM after the existing
output filter's bounded settling. The warm test must refresh IRQ level after its
test-only restore, because the calling loop sampled it before loading.

`knight_fall_sdl_probe.c` exercises the same saved reproducer in the actual SDL
loop in either compiled profile. Supply `--fixture <before-death.sav> --output
<scratch> --log <absolute scratch log>` with the normal `--os --sdl --dataset
<private data> --diskdir <private data>` arguments; `--full` tests fullscreen.
It asserts no return to kneeling after the fall, one life lost and normal HP
restoration. All probes/fixtures/logs are private and must never be packaged.

## Mudman spawn pixels

Build `mudman_spawn_probe.c` with the normal `build.py --probe` recipe. Its
`--native <private evidence folder> --log <absolute scratch log>` mode tests both
disk layouts against actual masked blits and unrelated-call/state guards.
`check_mudman_spawn.py --before-probe <frozen observer> --before-exe <old engine>
--evidence <private evidence> --output <scratch>` compares full fights, exact
save/RAM/PCM, edge-only picture changes, other deaths and cold/warm restores.
`--resume-fights` continues after already completed fight comparisons.

`mudman_spawn_sdl_probe.c` runs the actual SDL loop in either profile. Supply
`--fixture <first-pose.sav> --output <scratch> --log <absolute scratch log>` and
`--os --sdl --dataset <private data> --diskdir <private data>`; add `--full` for
fullscreen. Compare all captured pictures and final saves across profiles/modes.
Read `notes-handoff/moonstone-mudman-spawn.md` for private fixture locations and
scope. All probes and private evidence must stay out of public packages.

## Tavern quick taps

Build `tavern_input_probe.c` and `tavern_input_sdl_probe.c` with the canonical
`build.py <probe-name> --probe regress/<probe-name>.c` recipe, in both profiles.
`tavern_input_probe --native <port.ram> <retail.ram> --log <absolute scratch log>`
executes original RNG/payout/hotspot code and input/register/scope checks.

Run `check_tavern_input.py --fixture <private tavern-betting.sav> --before
<frozen preceding engine> --output <scratch>` for105 complete replays. Add
`--sdl-only` with a separate output folder for22 actual SDL cases: four owners,
keyboard/virtual controllers, focus/load/disconnect/reconnect. All cases use
absolute scratch logs and private save copies. The native acceptance time and
RNG/results must match the old input path; identical-input before/after controls
also require identical complete saves, RAM, PCM and pixels. Pending physical
input is intentionally discarded on load, while an already accepted native
transaction must survive cold/warm restores exactly once.

See `notes-handoff/moonstone-tavern-input.md` for private reference paths and
limits. Never distribute probes or their evidence/fixture folders.

## Spear execution freeze

Build `spear_death_probe.c` in both profiles using `build.py --probe`. Its
`--native <port.ram> <retail.ram> --log <absolute scratch log>` mode compares
actual native fatal contacts with an independent original-code installer
oracle and checks unrelated contacts/recovery/CCR guards. Run
`check_spear_death.py --evidence <private evidence> --output <scratch>` for
the saved freeze, controlled pre-fatal contact, other deaths and reloads.
`--saves-only` resumes completed fight checks. `--before-probe <frozen observer>`
checks a preexisting cold/warm cumulative-counter difference against the old
engine's exact full results; no new gameplay state differences are permitted.
See `notes-handoff/moonstone-spear-death.md` for fixture/provenance details.

`spear_death_sdl_probe.c` accepts `--fixture <stuck.sav> --output <scratch>
--log <absolute scratch log>` plus `--os --sdl --dataset <private data>
--diskdir <private data>`; `--full` selects fullscreen. Run in both profiles
and compare full final saves and captured pixels. All probes/private inputs
stay out of the public package.

## Public executable metadata

`check_pe_metadata.py --before <original-public-EXE-folder> --output <new-scratch>`
checks `pe_metadata.py` with the independently installed `pefile` parser. It
requires identical loaded code/data/resources and section permissions, an
unspecified PE timestamp, no empty REPRO section, idempotence, and rejection of
unexpected debug payloads, signatures and layouts. It also verifies the public
package gate rejects unprocessed files. Originals are read-only; this test does
not launch the game. Follow it with normal build-profile comparisons and startup
checks of the exact packaged EXEs. No production dependency is added.

The unchanged save launchers are independently versioned components. Run
`check_launcher_components.py --output <new-scratch>` after building release.
It accepts only their two approved exact hashes and rejects modified/wrong-slot
launchers, stale source provenance, development engines and attempts to apply the
launcher metadata exception to the game. `check_launchers.py` covers actual
child-process arguments, paths, profile selection, buffer limits and errors.

`check_engine_icons.py --before <PNG-icon-public-engine> --after <public-engine>
--output <new-scratch>` independently checks all six icon sizes, every RGBA pixel
and transparency-mask bit, original game payloads, imports/TLS/permissions,
reproducible resource conversion and idempotence. It rejects compressed icons
returning or resource processing changing executable code. It uses installed
Pillow/pefile test tools; the production build helper uses only the standard
library and Windows resource APIs. No engine is executed by this test.

## Adding a golden

1. Get the state into a save (e.g. F5 the win screen) under `dist/MoonstoneNative/`.
2. Add a `framesha` row with any placeholder golden.
3. **Confirm the state renders correctly**, then baseline it:
   `python recomp/regress/check.py --update`

## After an intentional change

A real behaviour/render change will (correctly) FAIL. Eyeball that the new output is what
you wanted, then re-baseline: `python recomp/regress/check.py --update`.
