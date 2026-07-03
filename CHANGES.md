# Moonstone Reborn — Changes vs. the 1991 Amiga Original

Native Windows port of *Moonstone: A Hard Days Knight* (Mindscape, 1991), running the
original 68000 game code on a from-scratch model of the Amiga's custom chips — no emulator,
no Kickstart ROM. This document catalogs what the port changes relative to the original game.

_Last updated: 2026-07-03 · build "Moonstone Reborn v1.0.0"._

---

## 1. Original-game bug fixes

Defects in the 1991 game itself that this port corrects.

- **Random life-loss at day-end** — An undocumented "curse" mechanic (the type-4 hazard event sets a +0x82 flag; one life drained per day-end until a paid healer visit clears it) fires spuriously after the black knights die. The mechanic appears in no manual or playthrough of the original, so the port suppresses it: the penalty body @0x264b8 is skipped (exiting through the actor-scheduler's proper handler contract) and any stuck flag is cleared at the day-end dock @0x2171c.
- **AI knight walks into the map edge / through the player** — The overland AI commits one straight-line trajectory per turn, but only its town-goal mode has an arrival check; trajectories toward the *fallback* map-object goal or a *chased* knight never stop, so a knight overshoots a nearby goal — grinding against the map edge, or strolling straight through the player he was hunting. The port adds the missing arrival: the knight stops at his committed goal (`--noedgewalkfix` restores the original behaviour).
- **AI knight reaches the player but never attacks** — The engagement machinery is all there: the mover rebuilds a "touching" queue every tick, and the per-tick arrival check ends in *"chase target in my queue → attack + end turn"*. But that check tests the knight's **town goal first**, and the gold-driven intent ladder (rich knights go shopping, poor ones go heal) re-arms the town goal *every tick* — so any knight with town business has his attack check masked for the entire chase, even as his steering marches straight at the player. He arrives on top of you… and thinks about shopping. The port routes arrival to the queue check on the contact tick only, so a knight who catches you attacks you; every non-contact tick is bit-identical to the original (knight-vs-knight pairs are still filtered by the game's own encounter handler, so no new AI infighting). `--nochaseengagefix` restores the pacifist original.
- **Town exit doesn't end the day after buying armor** — The town Exit "ends the turn" by stamping the turn timer to the *current* movement budget — but the budget is an equipment-derived stat (best armor raises it 96→128), re-derived on the post-exit map return. Buy the good armor in town and the re-derived, bigger budget outruns the stamp: the turn-over test (`timer ≥ budget`) never trips and your turn silently survives with 32 bonus ticks (the day only ends after that much extra walking — hence "intermittent"). Root-caused from a live capture (the armed TOWN-ACT tripwires) plus before/after saves; reproduced deterministically in the harness (`townarmor` golden). The port carries a pending turn-end across the budget re-derivation (`--nodayendfix` restores the original). The speed-potion budget doublers are untouched — extending an expiring turn is their designed effect.
- **Canopy-choke haul flings the monkey off-screen** — The swamp monkey's canopy-choke haul launches it +150 px right *unconditionally*, so a right-side grab lands it off a 320-wide arena (off-screen choke loop, clipped stab animation, HP drain). The haul now swings inward toward the arena centre (`--nochokefix` restores it).
- **AUD1 SFX cut short (stray DMACON write)** — The sound-start routine ends with a hard-coded `move.w #$2,DMACON` that kills AUD1's DMA on *every* voice start (a copy/paste slip — every other write in the routine is channel-indexed), chopping any one-shot SFX playing on AUD1; the stray instruction is skipped (`--noaud1fix` restores it).
- **Moonstone-in-enemy crash** — A black knight loots the Moonstone, then clicking it walks a corrupt inventory list into a wild dereference; guarded by blocking a Moonstone loot to a non-player winner, plus `sndcode_guard` containment and a precise deref trap @0x2a1b8. _(contained + guarded; pending another playthrough)_
- **Sound-engine code-write crash (blue-orb loot/equip)** — Hovering a blue orb over armor wrote a byte into the live sound-engine code, faulting on the next audio run; `sndcode_guard` drops any post-load store into the code region.
- **Two-troll / double-overhead-swing crash + post-combat cursor freeze** — Simultaneous starts of one screen-effect leaked task-list entries until the 9-slot list overflowed; fixed with idempotent effect registration + effect-only removal that never deletes the cursor/menu handler (`g_tasklist_fix`).
- **Inventory over-walk crash trap** — Clicking a leaked/corrupt inventory entry dereferences garbage; trapped with a clean snapshot+halt instead of corrupting the roster.
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
- **nb loader + AmigaOS exec/dos HLE** — Loads/relocates the modules with no Kickstart.
- **OCS blitter + Paula + trackdisk** — Full from-scratch chipset model.
- **ADF auto-extract** — If the boot modules are missing, reads them straight out of Disk 1's filesystem — a player only drops in the three `.adf` images.
- **Automatic disk swapping** — Inserts the requested disk + auto-confirms at "Please insert Disk X" (`--noautoswap` restores prompts).
- **Floppy motor/seek acceleration** — Collapses the ~8 s of modelled drive dead time at the intro→gameplay hand-off (`--noflopdelay` restores it).
- **Exe-relative paths, build stamp, crash reporter, unlimited live run.**

**Controls**

- **Gamepad support** — Plug-and-play Xbox/XInput (move, attack, cancel, inventory, rest); also drives the cursor for equip/altar menus.
- **Keyboard + mouse** — Arrows / mouse / Ctrl / Enter / click / Esc.
- **Inventory key** (Space / I / pad-Y / Start) and **REST / skip-turn key** (E / pad-Back) — both previously unreachable.
- **Typed custom knight name**, **numbered-menu quick-keys** (1–9 / Up-Down popups), and **intro skip / fast-forward** (fire/confirm key or any pad button).

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
