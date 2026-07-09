# Moonstone Reborn — Changes vs. the 1991 Amiga Original

Native Windows port of *Moonstone: A Hard Days Knight* (Mindscape, 1991), running the
original 68000 game code on a from-scratch model of the Amiga's custom chips — no emulator,
no Kickstart ROM. This document catalogs what the port changes relative to the original game.

_Last updated: 2026-07-03 · build "Moonstone Reborn v1.0.0"._

---

## 1. Original-game bug fixes

Defects in the 1991 game itself that this port corrects.

- **Random life-loss at day-end (the undocumented curse) — removed** — The original game contains a fully designed but completely undocumented curse: a type-4 overland hazard event drains HP and sets a curse flag (+0x82), after which the knight loses one life at *every* day-end, indefinitely. The designed cure is equally undocumented: a healer donation lifts it (the healer's donation handler explicitly tests and clears the flag and picks a dedicated dialog line). Nothing about it appears in the manual or any period walkthrough; the trigger behaves erratically (observed firing repeatedly once all black knights were dead; the event-selection logic was never fully mapped), so players can only experience it as an inexplicable life-draining bug. Removed by decision: the penalty body @0x264b8 is skipped (exiting through the actor-scheduler's proper handler contract) and any stuck flag in an existing save is cleared at the day-end dock @0x2171c. Distinct from Math the Wizard's time-limited "too ill to fight" punishment (+0x52), which self-cures over a few days and remains in the game.
- **AI knight walks into the map edge / through the player** — The overland AI commits one straight-line trajectory per turn, but only its town-goal mode has an arrival check; trajectories toward the *fallback* map-object goal or a *chased* knight never stop, so a knight overshoots a nearby goal — grinding against the map edge, or strolling straight through the player he was hunting. The port adds the missing arrival: the knight stops at his committed goal (`--noedgewalkfix` restores the original behaviour).
- **AI knight reaches the player but never attacks** — The engagement machinery is all there: the mover rebuilds a "touching" queue every tick, and the per-tick arrival check ends in *"chase target in my queue → attack + end turn"*. But that check tests the knight's **town goal first**, and the gold-driven intent ladder (rich knights go shopping, poor ones go heal) re-arms the town goal *every tick* — so any knight with town business has his attack check masked for the entire chase, even as his steering marches straight at the player. He arrives on top of you… and thinks about shopping. The port routes arrival to the queue check on the contact tick only, so a knight who catches you attacks you; every non-contact tick is bit-identical to the original (knight-vs-knight pairs are still filtered by the game's own encounter handler, so no new AI infighting). `--nochaseengagefix` restores the pacifist original.
- **Town exit doesn't end the day after buying armor** — The town Exit "ends the turn" by stamping the turn timer to the *current* movement budget — but the budget is an equipment-derived stat (best armor raises it 96→128), re-derived on the post-exit map return. Buy the good armor in town and the re-derived, bigger budget outruns the stamp: the turn-over test (`timer ≥ budget`) never trips and your turn silently survives with 32 bonus ticks (the day only ends after that much extra walking — hence "intermittent"). Root-caused from a live capture (the armed TOWN-ACT tripwires) plus before/after saves; reproduced deterministically in the harness (`townarmor` golden). The port carries a pending turn-end across the budget re-derivation (`--nodayendfix` restores the original). The speed-potion budget doublers are untouched — extending an expiring turn is their designed effect.
- **Player knight vanishes mid-combat (enemy "hits air")** — Combat sprite visibility is a hide flag that the grab/pounce choreography *EOR-toggles* (hide the carried knight, show him on release) — and a hidden actor's per-frame behaviour and input processing are skipped along with his sprite. The toggle discipline depends on perfectly matched enter/release parity; the state machine has paths that break it (the grab-resolve pair can fire twice for one grab when both grab globals alias the same knight; the re-pounce limit cycle flips repeatedly), and one odd toggle leaves the knight released-but-hidden **forever**: invisible, uncontrollable, unwinnable. Forensics on the captured specimen proved the whole failure is that single flag (re-showing it by hand revives the sprite). The port makes the intent explicit at the toggle choke point: enter sites *set* hidden, release sites *set* visible — bit-identical for every correct sequence, and the vanish becomes unreachable by construction (`--nohidefix` restores the fragile original; a `HIDEFIX` log line records any unpaired toggle it corrects).
- **Swamp monster wedges inside the knight, flickering** — Let a swamp monster jump point-blank into your body (walk above it as it leaps) and it idles embedded in the knight, strobing in and out every frame until you walk away. The contact handler resolves a touch by the monster's state bits — mid-leap becomes the head-latch grab, carry continues the carry — but the *point-blank hold* state (set when the pounce gate finds its anchor already within the 40px standoff: the standoff constant equals the gate threshold) falls through to "bounce off", which clears the very state the gate immediately re-sets: a one-frame ping-pong, wedged forever. The port routes point-blank-hold contacts into the game's own head-latch resolution — the monster grabs on and you shake it off like any normal latch (`--nopouncefix` restores the flicker).
- **Canopy-choke haul flings the monkey off-screen** — The swamp monkey's canopy-choke haul launches it +150 px right *unconditionally*, so a right-side grab lands it off a 320-wide arena (off-screen choke loop, clipped stab animation, HP drain). The haul now swings inward toward the arena centre (`--nochokefix` restores it).
- **Black-knight gold corruption at the knife restock (cracked-build repair)** — The commonly circulating cracked ADFs carry an *earlier build* of the game engine than the retail release (verified against SPS preservation images of the retail disks). That build's AI shopping code pays for each extra throwing knife with a byte-wide subtract that lands on the high byte of the knight's 16-bit gold — **−512 gold per knife** — driving knight purses deeply negative; the loot screen then shows a blank gold field, and clicking it fills the player to the 150 cap out of the negative purse. The retail build has the correct word-wide payment and a working 10-knife cap; the port now emulates exactly that retail transaction at the broken loop (byte-guarded on the broken build's exact instructions, so it is inert on retail or unknown data; `--nogoldfix` restores the corruption). Verified: the reproduced restock that previously went 18 → −496 gold with 2 knives now buys 9 knives at 2 gold each and lands the purse at exactly 0.
- **Invisible dragon rematch after losing to it** — Lose to the swooping dragon and its hunt stays armed: the fight handler's exits never clear the dragon-hunt flag, and nothing redraws the dragon on the map after a fight (its flight handler was uninstalled at fight entry) — leaving an *undrawn* dragon parked at the contact spot whose per-step collision test drags you straight back into the arena (a respawn landing inside its box re-fights instantly, reproduced). The handler ships this way in the original module, though the port's RNG-entropy fix is likely what made player-targeted dragon hunts frequent enough to hit it. The fight exit now runs the game's own stand-down (the same hunt-flag clear the map rebuild performs on other scene paths): a surviving dragon **returns to flight** — re-armed by the game's map-entry check as a fresh, visible attack run — and a slain dragon stays retired (`--nodragonfix` restores the ambush).
- **AUD1 SFX cut short (stray DMACON write)** — The sound-start routine ends with a hard-coded `move.w #$2,DMACON` that kills AUD1's DMA on *every* voice start (a copy/paste slip — every other write in the routine is channel-indexed), chopping any one-shot SFX playing on AUD1; the stray instruction is skipped (`--noaud1fix` restores it).
- **Phantom moonstones + inventory hover/click crash (unvalidated item ids)** — The knight-panel and loot-screen builders derive every item's hotspot and hover label from raw weapon/armor id fields with **no range check**. A garbage id renders a *phantom item*: weapon ids 0x1a–0x1c hover-label as the three **Moonstone** names — the infamous "black knight suddenly has a moonstone" — and taking one copies the garbage id onto your own knight. Ids further out push the hover label past the two 27-entry name tables, so merely *pointing at the item* walks garbage memory: an address-error runaway on real hardware (the classic inventory crash). **The rendering flaw is present in the retail build as well** (no delta in the panel builders between the two builds), so rare phantom sightings on boxed disks are expected; what made it epidemic on the circulating ADFs is the pre-release build's stale-goods shopping defect mass-producing garbage ids (an armor id written into the weapon field — found live in a player save). No real Moonstone is ever involved: exhaustive tracing shows every actual token path is player-sourced (the Guardian's random 1-of-4 grant to his slayer, key-gated; Math the Wizard's prize table contains no moonstones or keys; the only transfers are the loot merge and the Acquisition-scroll steal, both from a carrier). The port fixes both ends: the parity layer restores retail's shop code (the producer), and the renderer now validates ids — an out-of-domain weapon/armor field builds no sprite and no hotspot — plus the label walk is hardened (a bad pointer or runaway chain ends the walk cleanly). Reproduced and verified both ways from a live save (`--noinvmenufix` restores the phantom and the wedge). Real moonstones — carried, looted back from a thief, viewed in the dragon's hoard, bought and sold at the High Temple — render and transfer exactly as designed. Additionally, a Moonstone loot to a non-player winner is blocked, and watchdog traps (`INVLINK-SET`, `AIITEM-SET`, the 0x2a1b8 deref trap) remain armed to falsify or confirm any acquisition path not yet observed.
- **Sound-engine code-write crash (blue-orb loot/equip)** — Hovering a blue orb over armor wrote a byte into the live sound-engine code, faulting on the next audio run; `sndcode_guard` drops any post-load store into the code region.
- **Two-troll / double-overhead-swing crash + post-combat cursor freeze** — Simultaneous starts of one screen-effect leaked task-list entries until the 9-slot list overflowed; fixed with idempotent effect registration + effect-only removal that never deletes the cursor/menu handler (`g_tasklist_fix`).
- **Item stacks growing past the inventory display** — Every acquisition path (loot merge, loot-screen take, temple purchase) adds item counts with plain uncapped byte-adds — the corpse-loot merge even wraps at 255 — while the inventory panels clamp what they *show* (potions/gems/talismans 4, rings 3, scrolls 2). Stacks are now capped at those displayed limits at the acquisition sites; existing over-stacks are left untouched, they just can't grow (`--nostackcap` reverts). Knives keep the game's own 10 cap; the Sword of Sharpness, keys and moonstones are unaffected.
- **Tavern winnings bypass the 150-gold cap** — The game caps gold at 150 everywhere else (looting stops at exactly 150; the temple sell path clamps), but the dice-game payout (stake × combo multiplier, up to 30×) adds with no cap, growing gold without limit. The payout now clamps at 150 like every other income path (`--nogamblecap` reverts). The stake side was already safe (bets are affordability-checked).
- **CPU-detection illegal-instruction passthrough** — Mog's 68010+ probe (privileged `movec`) now lets the genuine 68000 exception fall through so its self-decrypting routine runs.

**Display / buffer-layout**

- **Memory-pool / display-buffer overlap** — The allocator based work-pools over Mog's code *and* the display double-buffers, so decompress overran code (ILLEGAL) and the compose blit copied onto itself (plane noise); pools re-based into free RAM on every program entry.

**Boot / hardware-model (native-runtime) fixes**

- **Beam-accurate VPOSR/VHPOSR** — Tight raster-wait loops hung boot; fixed with a cycle-accurate beam (453 cyc/line, 313 lines) in 160-cycle bursts.
- **CIA decode mask + timer + drive-ready** — Corrected the register decode mask (the 0xFF00FF bug broke fire + CIA-B), added Timer A/B, asserted /RDY.
- **VERTB interrupt gate** — Stopped vblank firing into nb's boot-guard before a real handler existed (rts-pops-0 crash); gated until a game-owned handler is installed.
- **68000 TRACE-exception emulation** — Enabled for Mog's single-stepping CPU probe.
- **Drive-seek /TRK0 + df1: drive-select** — Fixed an infinite head-step recalibrate loop and a drive-select bug where seek only fired on df0:.
- **Benign unmapped-access handling** — Unmapped reads return floating-bus 0/0xff instead of a fatal halt, so legit low-RAM code and uninitialised AI pointers no longer freeze boot/new-game.

---

## 1b. Retail-parity layer (default on; `--noretailparity` reverts)

The commonly circulating ADFs carry an **earlier, pre-release-ish build** of the game engine than the boxed release (verified against SPS preservation images of the retail disks — the retail build even carries a version tag, "v1.4"). Beyond isolated byte defects, the two builds *play differently*: Mindscape fixed bugs and rebalanced the game for retail. This layer brings the port's gameplay up to the retail build — a guarded in-RAM patch table for data/same-size code differences plus PC hooks for retail's inserted logic, all fingerprint-gated so it is inert on non-cracked game data.

**Defect repairs (retail fixed these; the cracked build shipped broken):**

- **Weapon-purchase corruption** — the 10-gold weapon rung's goods-write lost its relocation in the cracked build: it stomped low RAM *and* left the shop's goods variable stale, so the knight equipped garbage. Redirected to the goods variable, as retail does.
- **Knife-restock loop runs retail's own opcodes** — the two broken size suffixes (byte-wide gold pay = −512/knife; dead knife cap) are patched to the retail instructions; the earlier port hook (`--nogoldfix`) auto-inerts via its byte-guard and remains for parity-off runs.
- **Dead knights excluded from encounters** — a lives test that treated the 0xff (dead/retired) marker as "alive" widened to retail's signed compare.
- **Demon-fight corruption cluster** — a stale-pointer per-tick movement write, an action-timer written through a sound-call's parameter pointer into game *data* (a silent wild-write; the timer never armed), missing state-flag clears at fight init (stale hold-latches carried across fights), and an unmasked 8-entry table index. All four repaired as retail did.
- **Stale-state hygiene** — combat flag clears widened byte→word (the second flag byte stayed stale); shop-intent globals preset per evaluation (stale-intent hole); handler-list installs deduped (the cursor-install site; the effect site was already fixed by `g_tasklist_fix`, which matches retail's own dedup); a task-slot helper no longer clobbers the caller's register.
- **KO-latch** — retail latches a downed fighter and stops applying further hit damage/animation to them; ported host-side.

**Gameplay / balance (the retail experience):**

- **Stat-gain roll direction** — cracked succeeded on *high* rolls (gains accelerate); retail on *low* (diminishing returns).
- **Minimum damage floor** — retail clamps armor-reduced damage to ≥ 5; cracked could reduce hits to 0.
- **Shop economy** — Sword of Sharpness 52 → 100 GP, ring of protection 40 → 50 GP (sell prices follow at half), with the menu strings corrected — the two longer retail lines live in a small string pool placed in a dead data block the retail build deleted.
- **Knight AI** — knights drink healing potions when hurt at retail's much more eager thresholds (any damage / lives < 5); the healer visit is affordable at retail's gold > 15 for 15 GP (was > 25 for 25); knights **auto-use dragon scrolls** to sic the dragon on their quarry (the whole stage is dead code in the cracked build — re-activated with retail's gates); wander destinations bias toward the nearest map node; chase targets are cleared at fight staging (no stale chases through fights).
- **Quest-object placement** — the four marked objects are placed with an independent roll per map quadrant (the cracked build reused one roll for all four — correlated placement).
- **Loot** — the victor takes the loser's *better armor* (hp-adjusted by retail's armor-weight table, loser reset to base) instead of halving their gold; the best-weapon loot entry is listed retail's way; demon close-range timing and aggression thresholds retuned to retail.
- **Cosmetics** — the fourth knight is retail's **SIR GUNTHER** (the cracked build's "SIR EDWARD"); the overland **'V' key shows the game version tag ("v1.4")** exactly as retail's handler does.

- **UI feedback sounds at retail's coverage** — retail plays its interface click (sound id 0x9c, register-preservingly wrapped) at ~27 loot/trade/select-screen actions; the early build at only a few of them. The 21 cleanly mappable retail-only sites are ported via a fully transparent guest call into the game's own sound trigger (all registers + SR saved and restored around it — verified on a live loot screen: game state, roster, and display pixels byte-identical with the feature on vs off, only the sound engine's voice state differing; `--noretailsfx` for A/B). Three retail sites already have cracked-build twins; three sit inside a structurally different menu flow and are left unported.

**Non-deltas and deliberate omissions:** the practice-arena monster select works via unlabeled keys **in retail too** — retail contains a labeled menu for it ("B - Balok … G - Guardian") as *dead code*, never called (an unfinished feature; verified: no reference of any kind reaches it), so the port matches actual retail behavior as-is. Retail's animation-descriptor data revision is not carried (entangled with pointer tables). The retail loader's sector-checksum verify is irrelevant to the port (clean data by construction).

**Existing fixes re-evaluated against retail** (the "which of ours are still needed" audit): every previously shipped fix addresses either a bug present in *both* builds (choke-haul, pounce-latch, hide-parity/vanish, AUD1 DMACON, edge-walk, chase-engage, day-end TOCTOU — retail restructured that code; our fix is the cracked-side equivalent) or a port-level concern (RNG entropy, guards, QoL) — all stay. `g_dragon_fix` deliberately goes *beyond* retail: the boxed game still never clears the dragon-hunt flag after a lost fight (the invisible-rematch ambush), ours does. One deliberate divergence remains: the **type-4 "curse" life-drain is suppressed** (see section 1) even though retail also ships that mechanic — restoring it for strict retail behavior is a one-line decision if ever wanted.

---

## 2. Faithfulness fixes

Glitches the port introduced in reproducing the Amiga, fixed to match real hardware.

**Gameplay / randomness**

- **RNG seed entropy (identical loot every run)** — The game seeds its RNG once per new game by reading the raster beam position (`VHPOSR & 3` picks one of four preset seed streams); on real hardware the player's fire-press arrived at an arbitrary raster line, so runs differed — in the port, input is sampled at a fixed point in the frame, so the same seed was picked every run and zone loot never varied. Live play now substitutes host timing entropy at the seeding instant (same four faithful streams); headless/regression runs stay fully deterministic (`--norngseedfix`).

**Audio**

- **AUDxVOL byte-write duplication** — A `move.b` to even AUDxVOL wrote only the high byte → soft per-note voices (the intro trumpet echo) blasted at full; now duplicates the byte onto both bus halves so per-note volume lands faithfully.
- **Deferred-reload one-shot SFX (played twice)** — One-shot SFX auto-reloaded before the ISR repointed to its silent loop; fixed with interleaved generation + a one-tick deferred reload.
- **Combat "screech" / garbage-loop** — Non-atomic LCH/LEN writes latched a silent-loop address with the old length (loud low-RAM burst); now waits for AUDxLEN to be rewritten (`g_len_armed`).
- **DMA-restart prime** — A re-triggered channel emitted the previous buffer's held sample; now primes the DAC from the new buffer's first byte — removed the forest crackle, restored the lightning/monk-chant SFX attacks.
- **Amiga RC + LED output filters** — A one-pole ~5 kHz RC (the A500's always-on analog filter) plus an optional 2-pole Butterworth "LED" filter, rounding the raw zero-order-hold attack edges (`--lpf` / `--ledfilter`).
- **SDL pause-until-audible + prime** — Fixed a chronic audio-queue underrun during the ~33 s silent load (holds the device paused on a silence cushion until the first audible frame).
- **Intro A/V audio-output delay** — Shifts attract audio ~−135 frames vs the blit-paced visuals so audio doesn't race ahead of the lagging composite (`--avdelay`).
- **Stereo crossfeed** — Blends ~35% of each hard-panned Paula side into the other so melodies don't ping-pong between the ears (`--hardpan` / `--audioblend`).

**Display**

- **Unified buffer-garble gate (BACKBUF-RECOVER)** — Win-screen-shows-map, "The End"-shows-constellation, and legend/transition flashes were one bug (attract composes to master 0x80000, copies to the display halves a few frames late); recovery now gated to the attract compose phase only.
- **Fully-animated intro + legend crawl** — The loader-HLE trap shared a PC with the XOR char/object engine, discarding all animated content (monks/credits/legend "Quest for the MOONSTONE" crawl); fixed with an opcode-signature guard.
- **Outro win screen + "The End"** — bg8.piv "You have completed the quest" and "The End" now show correctly (removed an auto-forward, gated off the stale-buffer swap).
- **Moonstone-delivery & give-ritual garbles** — Palette/buffer/fill-keyed holds so the delivery maps and give-item cutscene don't flash stale bitplanes.
- **Vblank-aligned frame capture** — Snapshots at the VBlank instant so the copper/buffer table is complete (no torn-list noise).
- **BPLxPT zero-selector table + sprite compositing** — Reads Mog's packed bitplane-pointer table (not a conventional copper list) and composites the 8 OCS hardware sprites (incl. the mouse pointer) by walking the copper itself (`--spritex`).
- **JOY0DAT quadrature mouse** — Exposes the port-0 mouse as a free-running counter + right button via POTGOR, matching how the game reads it.
- **Trackdisk DMA from ADF (faithful MFM)** — Serves raw trackdisk reads MFM-encoded exactly as the decoder expects, head position modelled from CIA-B /STEP pulses.

---

## 3. Improvements / additions

Features beyond the original game.

**Runtime / packaging**

- **Native, no emulator** — Double-click `moonstone.exe`; runs on Windows with no WinUAE/FS-UAE and no Amiga ROM. The folder is portable.
- **Intro pacing (post-credits warp)** — The moon backdrop is the boot loading screen: the credit pages flip at load milestones while the animated intro loads from "disk", and once the last credit has shown, the bare moon just sits there for ~13 s while the loader grinds on. The credit roll itself is left **completely original** — every page, at the authentic pace. Only after the game's own sequencer finishes the last page does the port warp the remaining load (boosted CPU charge for the loader plus collapsed head-seek settle waits), cutting the empty moon-gazing to ~4 s and starting the animated intro ~10 s sooner. Nothing after the intro program launches is touched. Tunables: `--bootboost N` (warp factor, 1 = fully authentic), `--credpace N` (optionally also enforce a minimum per-page time; off by default).
- **nb loader + AmigaOS exec/dos HLE** — Loads/relocates the modules with no Kickstart.
- **OCS blitter + Paula + trackdisk** — Full from-scratch chipset model.
- **ADF auto-extract** — If the boot modules are missing, reads them straight out of Disk 1's filesystem — a player only drops in the three `.adf` images.
- **Invisible disk swapping** — The three-floppy original stops at "Please insert Disk X — press fire" on every disk crossing (each town visit, each combat arena, the boot hand-off). The port inserts the requested disk instantly *and never shows the prompt at all*: the prompt screen is skipped outright and the game's own confirm/validate loop runs invisibly under the load backdrop (`--noswapskip` shows the auto-confirmed prompt flash again; `--noautoswap` restores fully manual prompts).
- **Floppy motor/seek acceleration** — Collapses the ~8 s of modelled drive dead time at the intro→gameplay hand-off (`--noflopdelay` restores it).
- **Exe-relative paths, build stamp, crash reporter, unlimited live run.**

**Controls**

- **Gamepad support** — Plug-and-play Xbox/XInput (move, attack, cancel, inventory, rest); also drives the cursor for equip/altar menus.
- **Keyboard + mouse** — Arrows / mouse / Ctrl / Enter / click / Esc.
- **Inventory key** (Space / I / pad-Y / Start) and **REST / skip-turn key** (E / pad-Back) — both previously unreachable.
- **Typed custom knight name**, **numbered-menu quick-keys** (1–9 / Up-Down popups), and **intro skip / fast-forward** (fire/confirm key or any pad button). The skip lands directly on the main menu — it fast-forwards through the ~10 s post-intro loader black, its looping chant, and the "Loading..." title card instead of dropping the player into them (`--noskipmenu` restores the old landing at the loader). A fully watched intro cuts to the menu the same way at its natural end, right after the final scene fades out (`--nointrocut` restores the watched-intro loader wait).

**Audio**

- **Natural stereo by default** (`--audioblend` / `--hardpan`) and **F12 live audio capture** (capture-N.wav + register trace).

**Presentation**

- **Window / taskbar icon** (the credit-scene moon) and a **boot splash** during the disk load instead of a black screen.
- **Boot / scene-transition holds** (hide the uninitialised-buffer specks until the Mindscape logo) and **`--scale N`** window sizing.

**Save system**

- **Quicksave / Quickload (F5 / F9)** — Whole-machine snapshot to `moonstone.sav`, anywhere including mid-combat — the 1991 release had no saving at all.

**Verified-working systems** (no engine fix needed): multiplayer (2–4 knights), the win path, town/service screens, and the wilderness combat loop were all reverse-engineered and confirmed end-to-end.

---

## Removed / not shipping

Internal dead-ends and band-aids that were tried and reverted; not in the shipping build.

- **Credits-scroll speed-hack** — page-flip credit-roll slowdown, reverted to vanilla speed.
- **Dagger-throw binding (pad X / 'X')** — synthesised combo that didn't work in live combat; stripped.
- **`--trumpetmode`** — superseded by the AUDxVOL byte-write root fix; left inert as a diagnostic A/B.
- **Life-loss / moonstone-in-enemy band-aids** (`g_lifeguard`, load-time scrub, per-frame enemy-Moonstone wipe) — superseded by the root guards above.
- **Per-channel declick filters** (`--boomlp`, `--aud1lim`) — built during an intro right-ear-tick investigation; left inert (default-off). The tick is a faint, in-sample audio artifact that resisted every clean fix; the investigation is **shelved**, the diagnostic tooling kept for a possible future revisit.
