/* Moonstone Reborn — a native (no-emulator) port of "Moonstone: A Hard Days
 * Knight" (Amiga, 1991).  The original 68000 game code runs under an embedded
 * Musashi CPU core on top of a from-scratch model of the Amiga OCS chipset
 * (blitter, copper, bitplane display, hardware sprites, Paula audio, CIA,
 * trackdisk).  No emulator, no Kickstart ROM.
 *
 * Copyright (C) 2026 Undine1  <github.com/Undine1>
 * Project home: https://github.com/Undine1/moonstone-reborn
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 3 as published by the Free
 * Software Foundation.  This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License (LICENSE) for more details.
 *
 * The original Moonstone game and its data are NOT part of this program and are
 * not distributed with it; you must supply your own legally-owned copy.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "m68k.h"
#include "loader.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "moon_icon.h"   /* embedded credit-scene moon, set as the SDL window/taskbar icon */
#include "splash_image.h" /* embedded boot splash, shown during the black disk-load */
#include "qoi_dec.h"      /* ~60-line QOI decoder for the splash (no external image lib) */
#define SPLASH_BOOT_NZ 2000  /* nonzero display bytes that mean "intro is up" -> hand off the splash */

/* Project identity / attribution.  Printed at startup (to the log) and via
 * --version; also serves as the binary's attribution string. */
#define MOON_ATTRIB "Moonstone Reborn v1.0.0 - native (no-emulator) port of " \
    "Moonstone: A Hard Days Knight (Amiga 1991) - (C) 2026 Undine1, " \
    "github.com/Undine1/moonstone-reborn - GPL-3.0"
/* Compile timestamp, shown in the window title + log so it's unambiguous WHICH
 * build is actually running (no more "did my change take effect?" guesswork). */
#define MOON_BUILD (__DATE__ " " __TIME__)

/* Directory containing the executable (filled in main() from the OS), used to
 * resolve the bundled `data/` folder so the game runs by double-click from any
 * location -- not only from the dev `recomp/` working dir.  Empty until set. */
static char g_exedir[1024] = "";
static void compute_exedir(const char *argv0) {
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        char *slash = NULL;
        for (char *p = buf; *p; p++) if (*p=='\\'||*p=='/') slash = p;
        if (slash) { *slash = 0; snprintf(g_exedir, sizeof(g_exedir), "%s", buf); return; }
    }
#endif
    /* fallback: derive from argv0 */
    if (argv0 && *argv0) {
        snprintf(g_exedir, sizeof(g_exedir), "%s", argv0);
        char *slash = NULL;
        for (char *p = g_exedir; *p; p++) if (*p=='\\'||*p=='/') slash = p;
        if (slash) *slash = 0; else g_exedir[0] = 0;
    }
}
/* Does `dir` exist and contain a recognizable game data file? */
static int dir_has_data(const char *dir) {
    if (!dir || !*dir) return 0;
    char p[1100];
    snprintf(p, sizeof(p), "%s/program", dir);   /* the loaded `program` module */
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* digital joystick + fire state (set by host input) */
static int g_ji_up, g_ji_dn, g_ji_lf, g_ji_rt;   /* directions */
static int g_fire2;                               /* port-1 fire (/FIR1) */
static int g_kdigit = 0;        /* SDL number key 1-9 held (1..9), else 0 (menu selection) */
static int g_inv_request = 0;   /* host pressed the inventory key (Space/I/padY/padStart); injected at the map poll */
static int g_in_inventory = 0;  /* 1 while the inventory screen is open (so the open-key can't re-open it in a loop) */
static int g_rest_request = 0;  /* host pressed REST/skip-turn (E / pad Back); injects scancode 0x12 ('E') at the map poll */
static int g_rest_pending = 0;  /* 1 for the one poll AFTER injecting 'E', to re-clear [0x3bf74] so one press = exactly one skipped turn */
static int g_firewait_hot = 0;  /* set when the "wait for fire" routine (0x22fd0) ran this frame; cleared each frame in capture_frame */
static int g_mappoll_hot = 0;   /* set when the normal map turn-loop's keyboard poll (0x4024c) ran this frame; the delivery overview is a SEPARATE loop that never hits it -- so firewait && !mappoll = the garbled delivery overview */
/* MOONSTONE-DELIVERY garbled-map latch (2026-06-24).  The "return matching Moonstone to its home
 * village" handler (node 0x1b) branches to 0x22820 when the returned token matches; from 0x22876 it
 * displays the overland map (scene 9), waits for fire (0x2288a), then 0x231e6 + the 0x2289e relaunch
 * reload `program` and play the season cutscene.  Through that whole window the REAL map bitplanes stay
 * on-screen with the cutscene's WRONG palette (lower-16 teals/creams) loaded over them -> the two
 * garbled "maps".  Scene-bracketing this failed repeatedly: the scene value bounces 9->0(loading)->9
 * ->0(cutscene), so neither the handler-end PC nor a sustained-scene-0 reliably marks the cutscene.
 * KEY (operator's original hint): the garbled map is distinguished by its WRONG PALETTE, not the scene.
 * So: snapshot the GOOD overland-map palette (COLOR0..15) every normal-map frame; on 0x22820 open a
 * generous frame WINDOW; during the window, BLACK OUT any scene-9 frame whose palette DIFFERS from the
 * snapshot (= the garbled map) while leaving a correctly-coloured map (post-cutscene, palette matches)
 * and the cutscene (scene 0, not scene 9) untouched.  0x22820 is reached ONLY for this delivery, and
 * the palette test means even if the window overruns into normal play nothing good is blacked. */
static int      g_delivery_win = 0;     /* >0 = delivery window active (frame countdown from 0x22820) */
static int      g_winwait_frame = -1;   /* g_cur_frame when the win-screen fire-wait first armed (3s auto-advance timer) */
static uint16_t g_map_pal[16] = {0};    /* last-good overland-map palette (COLOR0..15), snapshotted in normal play */
static int      g_dbg_paldiff = 0;      /* diag: last scene-9 delivery-window palette diff vs g_map_pal */
static double   g_dbg_coh = 0.0;        /* diag: last scene-9 delivery-window bitplane coherence */
static int g_menusel_prev = 0;  /* edge-state for the overlap-popup selection */

/* ---- typed-text entry (Select-Knight name field + any [0x3bf74] text poll) ----
 * The name-entry FSM (front-end 0x22b84, byte-identical Mog copy 0x1c86d6) busy-polls
 * the keyboard buffer word [0x3bf74]: 0 = no key; 0x1c = Return/confirm; 0x0e =
 * Backspace; otherwise it is an index into the 0xc1b3 table (routine 0x3ff42) that
 * yields the printable ASCII appended to the name buffer [0x30300].  On a real Amiga
 * that buffer is written by the CIA-A SP keyboard interrupt (the installed L2/PORTS
 * handler at [0x68]=0x3ba20 reads CIA-A SDR $bfec01, de-rotates the wire byte and
 * stores the 0xc1b3-index).  We deliver typed keys faithfully in the SAME value space
 * the game consumes: a small FIFO of 0xc1b3-indices, drained one-per-poll directly
 * into [0x3bf74] at the name-entry poll site (mirrors the proven overlap-popup digit
 * injection at 0x41178).  The map below is the INVERSE of the 0xc1b3 table (so it is
 * self-consistent with the game's own translate): the index whose 0xc1b3[idx]==the
 * key's ASCII.  Return=0x1c / Backspace=0x0e / Space=0x39 are the control indices the
 * FSM checks directly.  All A-Z/0-9/space round-trip-verified against 0xc1b3. */
#define KEYQ_SZ 32
static uint8_t g_keyq[KEYQ_SZ];   /* queued 0xc1b3-indices (raw [0x3bf74] values) */
static int g_keyq_head = 0, g_keyq_tail = 0;
static void keyq_push(uint8_t idx) {
    int n = (g_keyq_tail + 1) % KEYQ_SZ;
    if (n != g_keyq_head) { g_keyq[g_keyq_tail] = idx; g_keyq_tail = n; }
}
static int  keyq_empty(void) { return g_keyq_head == g_keyq_tail; }
static uint8_t keyq_pop(void) { uint8_t v = g_keyq[g_keyq_head]; g_keyq_head = (g_keyq_head+1)%KEYQ_SZ; return v; }
/* SDL keysym -> [0x3bf74] index (0xc1b3 inverse).  Returns 0 if the key is not a
 * text-entry key (so it is ignored by the text path and left to the normal handlers). */
static uint8_t keysym_to_idx(int sym) {
    switch (sym) {
        case 'a': return 0x1e; case 'b': return 0x30; case 'c': return 0x2e; case 'd': return 0x20;
        case 'e': return 0x12; case 'f': return 0x21; case 'g': return 0x22; case 'h': return 0x23;
        case 'i': return 0x17; case 'j': return 0x24; case 'k': return 0x25; case 'l': return 0x26;
        case 'm': return 0x32; case 'n': return 0x31; case 'o': return 0x18; case 'p': return 0x19;
        case 'q': return 0x10; case 'r': return 0x13; case 's': return 0x1f; case 't': return 0x14;
        case 'u': return 0x16; case 'v': return 0x2f; case 'w': return 0x11; case 'x': return 0x2d;
        case 'y': return 0x15; case 'z': return 0x2c;
        case '0': return 0x0b; case '1': return 0x02; case '2': return 0x03; case '3': return 0x04;
        case '4': return 0x05; case '5': return 0x06; case '6': return 0x07; case '7': return 0x08;
        case '8': return 0x09; case '9': return 0x0a;
        case ' ': return 0x39;                 /* Space  */
        case 0x08: return 0x0e;                /* SDLK_BACKSPACE -> index 0x0e (FSM backspace) */
        case 0x0d: return 0x1c;                /* SDLK_RETURN    -> index 0x1c (FSM confirm)   */
        default: return 0;
    }
}
/* Intro/legend text-overlay state: the attract front-end (`program`) renders the
 * legend story-crawl with its XOR-mode char engine (text renderer 0x28ed2 -> char
 * blit 0x2ca80).  In that mode each glyph is assembled in a scratch pool and the
 * char-blit's own per-glyph "cookie-cut onto the display planes" tail (the code at
 * 0x2cda6..0x2ce4e, dest = the display-plane table $2cf96) is NOT executed -- the
 * routine returns after assembling the glyph (path verified: across the whole
 * legend, every blit targets the scratch pool $2d616=0xab82, ZERO reach the display
 * half 0x6bdfa, and the cookie-cut entry at 0x2cda6 never runs).  So the legend
 * glyphs are built but discarded, and the displayed vortex shows no text.  The
 * in-game / non-intro text paths are unaffected (they use the Mog engine, a
 * different routine that does reach its display copy).  Fix: while the legend text
 * scene is active, let the char-blit fall through into its own cookie-cut tail so
 * each glyph blits onto the display half -- this runs the GAME's own blit, just
 * unskipped, so the rendered text is faithful.  Gated on g_os + the legend window. */
static int g_legend_active = 0;   /* set in the legend text scene (0x221ca..Mog launch) */
static uint16_t encode_joy(void) {
    /* Amiga digital-joystick encoding read from JOYxDAT:
       right=bit1, left=bit9, down=bit0^bit1, up=bit8^bit9 */
    int b1 = g_ji_rt ? 1:0;
    int b9 = g_ji_lf ? 1:0;
    int b0 = (g_ji_dn ? 1:0) ^ b1;
    int b8 = (g_ji_up ? 1:0) ^ b9;
    return (uint16_t)((b9<<9)|(b8<<8)|(b1<<1)|b0);
}

/* ---- mouse model (JOY0DAT in port-0 quadrature/counter mode) ----
 * Moonstone's front-end reads JOY0DAT ($DFF00A) as a MOUSE: the high byte is a
 * Y counter, the low byte is an X counter; the game keeps the previous value
 * and uses the signed delta (see routine 0x2a290). So we expose a free-running
 * counter that we nudge by g_mouse_dx/g_mouse_dy each time it is read while a
 * direction is requested.  POTGOR bits report the right mouse button.
 * Fire (left button) stays on CIA-A PRA bit6 (/FIR0). */
static int      g_mouse_x = 0x80, g_mouse_y = 0x80;  /* live 8-bit counters */
static int      g_mouse_dx = 0, g_mouse_dy = 0;      /* per-read nudge (host input) */
static int      g_rmb = 0;                            /* right mouse button */

static uint16_t read_joy0(void) {
    /* advance the counters by the requested nudge each read so the game's
     * delta-tracker (0x2a290) sees smooth motion while a direction is held. */
    g_mouse_x = (g_mouse_x + g_mouse_dx) & 0xff;
    g_mouse_y = (g_mouse_y + g_mouse_dy) & 0xff;
    return (uint16_t)(((g_mouse_y & 0xff) << 8) | (g_mouse_x & 0xff));
}
static uint16_t read_potgor(void) {
    /* POTGOR: bit10 = right mouse button (port0), active low. high bits set. */
    uint16_t v = 0xff00;
    if (!g_rmb) v |= 0x0400; else v &= ~0x0400;
    return v;
}
static void inlog(const char *what, uint16_t v);  /* fwd */

/* ------------------------------------------------------------------ memory */
#define RAM_SIZE   0x200000u            /* 2 MB chip RAM */
#define CUSTOM_LO  0xDFF000u
#define CUSTOM_HI  0xE00000u
#define CIA_LO     0xA00000u
#define CIA_HI     0xC00000u

static uint8_t  g_ram[RAM_SIZE + 4];    /* +4 GUARD bytes (2026-07-02): several helpers mask
                                         * the base index once and then touch a+1..a+3
                                         * (r16/r32, w16/w32, blt_r16/blt_w16, the Paula
                                         * OVER-READ detector), so an access at the top word
                                         * of chip RAM spills up to 3 bytes past RAM_SIZE --
                                         * an OOB read, or worse a WRITE into g_custom (next
                                         * object).  The guard absorbs those spills: reads
                                         * see 0, writes land in dead space.  Every bulk op
                                         * (save blob, ram dumps, memset, load_hunk) sizes
                                         * itself with RAM_SIZE explicitly, so formats and
                                         * determinism are unchanged. */
static uint16_t g_custom[0x100];        /* word registers at DFF000.. */
static uint8_t  g_ciaa[16], g_ciab[16];
static int      g_os;                   /* fwd: AmigaOS HLE enabled (defined below) */
static int      g_rdbg;                  /* fwd: --rdbg render/copper diagnostics */
static int      g_recover;               /* fwd: --recover RE-ENABLE the front-buffer recovery heuristic
                                          *  (OFF by default now that the pool layout is fixed; the real
                                          *   display buffer renders all 5 planes correctly).  --norecover
                                          *   is kept as a no-op alias for back-compat. */
static int      g_inlog;                 /* fwd: --inlog log input-register polls (PC+icount) */
static int      g_rawcapture;            /* fwd: --rawcapture -- bypass BOTH the empty-backbuffer
                                          *  recovery AND the transition-hold; capture EXACTLY the
                                          *  buffer the copper/0x7f6ae BPLxPT table points at, every
                                          *  frame.  Diagnostic: proves whether the heuristics are
                                          *  what froze the animated intro frames. */

/* synthesized video beam so polling loops make progress */
static uint64_t g_cycles = 0;

/* ---- blitter busy-time model (A/V sync) -----------------------------------
 * We execute a blit instantly (in blitter_run / blitter_line), but the real
 * OCS blitter holds DMACONR.BBUSY (bit14) set for the duration of the blit,
 * during which CPU code SPINS in `btst #14,$dff002; bne` wait loops, burning
 * a large share of each frame.  Without this, our CPU does far more work per
 * frame than a real Amiga during the blitter-heavy attract intro, so the
 * CPU-paced visuals outrun the vblank-locked audio: the red-knight lightning
 * FLASH (blit-paced animation) finishes ~9 s before its thunder SFX (whose
 * trigger is paced by the scene script's vblank/music-locked frame counter),
 * and the thunder ends up firing on a held, non-flashing pose.  Asserting
 * BBUSY for `cost` cycles after each blit makes those wait loops consume
 * cycles, throttling the blit-paced flash back into step with the thunder.
 * Measured (red-knight dense-flash end vs first thunder one-shot, frames):
 *   cyc/word  1    6    8    10   12
 *   gap      455  335  169  173  ~0
 * The pixel output is unchanged -- this is timing only.
 *
 * SCOPE: enabled ONLY during the attract intro (g_os && before the Mog engine
 * launches).  It is DELIBERATELY disabled for the Mog disk-loader and in-game:
 * the loader decompresses via blit->BLTBUSY-wait->blit, and adding dead wait
 * time there (where, unlike the intro's free-running animation, the real HW
 * was doing the decompression work during the wait) both slows the load and --
 * at some cyc/word values -- desyncs the loader's disk-DMA/IRQ handshake into a
 * stack-corruption crash.  The A/V drift is an attract-intro symptom; scoping
 * the model to the attract fixes it without destabilising the loader/gameplay.
 *
 * Cost = words_processed * g_blt_cyc_per_word. */
static uint64_t g_blt_busy_until = 0;     /* g_cycles value at which BBUSY clears */
static int      g_blt_cyc_per_word = 0;   /* tunable (--bltcpw): cycles charged per blit word.
                                            * DEFAULT 0 = no blit throttle (full speed): cost = w*h*0 = 0,
                                            * so BBUSY clears immediately.  Lever kept inert; the A/V-sync
                                            * fix is now the intro audio-output delay (see g_av_delay_frames). */
static int      g_blt_busy_scope = 0;     /* 1 while the busy model is active (attract intro only) */
/* Uniform DMA cycle-steal model (primary A/V-sync lever for the attract intro).
 * A real Amiga loses a big, roughly uniform share of CPU cycles to display/audio
 * DMA + blitter cycle-stealing every frame, so the real CPU executes FEWER
 * instructions per 1/50s frame than ours does.  We model that by advancing the
 * frame/beam/CIA clock by `did + stolen` while the CPU only ran `did` instrs:
 * the per-frame loop reaches CYCLES_PER_FRAME (full beam sweep) after ~10% fewer
 * CPU instructions, slowing ALL CPU-paced content (credit-line dwell + lightning
 * visual) uniformly -- unlike the blit-busy model, which only touches blit-heavy
 * code.  Scoped to the attract via g_blt_busy_scope (loader + gameplay untouched).
 * Tunable --dmasteal N (percent of executed cycles added as "stolen"). */
static int      g_dma_steal_pct = 0;      /* tunable (--dmasteal): % extra clock per CPU burst.
                                            * DEFAULT 0 = no cycle-steal (full speed): stolen = did*0 = 0.
                                            * Lever kept inert (see g_blt_cyc_per_word). */
static uint32_t g_vpos = 0, g_hpos = 0;
/* cycle-accurate PAL beam (drives VPOSR/VHPOSR so raster-wait loops work) */
#define LINES_PER_FRAME  313
#define CYCLES_PER_LINE  453      /* 141876 / 313 */
static uint32_t g_frame_cycle = 0;
static uint32_t g_beam_line = 0, g_beam_hpos = 0;
static void beam_update(void) {
    uint32_t line = g_frame_cycle / CYCLES_PER_LINE;
    if (line >= LINES_PER_FRAME) line = LINES_PER_FRAME - 1;
    g_beam_line = line;
    g_beam_hpos = (g_frame_cycle % CYCLES_PER_LINE) >> 1;   /* color clocks */
}

/* logging / control */
static int      g_trace = 0;            /* instructions left to trace */
static uint64_t g_icount = 0;
static uint64_t g_run_budget = 0;   /* 0 = unlimited (live play); headless bounded by --frames */
static int g_xfeed = 35;            /* stereo crossfeed % (0=faithful hard-pan, 50=mono).  Default 35.
                                     * (Tried 0/hard-pan 2026-06-28 to chase a right-ear "tick" -- did NOT
                                     * remove it, so the crossfeed is NOT the cause; reverted.) */
static int      g_stop = 0;
static const char *g_stop_reason = "?";
static uint32_t g_unmapped = 0;
static FILE    *g_log;

/* bus-access logging counters to keep noise bounded */
static uint32_t g_custw_log = 0, g_custr_log = 0, g_ciaw_log = 0, g_ciar_log = 0;
static uint32_t g_streamlog = 0;   /* HLE stream-read log budget */

static void halt(const char *why) { g_stop = 1; g_stop_reason = why; m68k_end_timeslice(); }
static int  vertb_gate(void);   /* fwd: gates VBlank IRQ injection (see below) */

/* --inlog: record input-register polls (which reg, value, PC, icount). Used to
 * discover empirically which input path the title/menu actually polls. */
static uint32_t g_inlog_n = 0;
static void inlog(const char *what, uint16_t v) {
    if (!g_log || g_inlog_n >= 4000) return;
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PPC);
    fprintf(g_log, "INPUT %-8s = %04x  pc=%06x ic=%llu\n",
            what, v, pc, (unsigned long long)g_icount);
    g_inlog_n++;
}

/* ----------------------------------------------------------- chip helpers */
static const char *creg_name(uint32_t off);   /* fwd */
static void update_ipl(void);                  /* fwd */
static inline void w8(uint32_t a, uint8_t v);  /* fwd (trackdisk DMA writes RAM) */
static inline uint16_t r16(uint32_t a);         /* fwd (trackdisk DMA diagnostics) */
static inline uint32_t r32(uint32_t a);         /* fwd (SCREECH-CTX stack read) */

/* Map current INTENA/INTREQ to a 68k interrupt level and assert it. */
static void update_ipl(void) {
    uint16_t ena = g_custom[0x09a>>1];
    uint16_t req = g_custom[0x09c>>1];
    int level = 0;
    if (ena & 0x4000) {                 /* INTEN master enable (bit14) */
        uint16_t a = (uint16_t)(ena & req & 0x3fff);
        if      (a & 0x2000) level = 6; /* EXTER (CIA-B)        */
        else if (a & 0x1800) level = 5; /* DSKSYNC / RBF        */
        else if (a & 0x0780) level = 4; /* AUD0-3               */
        else if (a & 0x0070) level = 3; /* COPER / VERTB / BLIT */
        else if (a & 0x0008) level = 2; /* PORTS (CIA-A)        */
        else if (a & 0x0007) level = 1; /* TBE / DSKBLK / SOFT  */
    }
    m68k_set_irq(level);
}

static uint16_t custom_read(uint32_t off) {
    off &= 0x1fe;
    switch (off) {
        case 0x002: {                                   /* DMACONR */
            /* BBUSY (bit14): set while the modelled blit is still "running".
             * Game wait loops (btst #14; bne) spin here, burning CPU cycles so
             * the attract intro's blit-paced visuals stay in step with the
             * vblank-locked audio.  Active only in the attract scope (see
             * g_blt_busy_scope); outside it BBUSY reads 0 (instant blit). */
            uint16_t v = (uint16_t)(g_custom[0x096>>1] & ~0x4000);
            if (g_blt_busy_scope && g_cycles < g_blt_busy_until) v |= 0x4000;
            return v;
        }
        case 0x004: { uint32_t v=g_beam_line; return (uint16_t)(((v>>8)&1)<<15)|((v>>8)&1); } /* VPOSR (V8) */
        case 0x006: return (uint16_t)(((g_beam_line&0xff)<<8)|(g_beam_hpos&0xff));            /* VHPOSR */
        case 0x00a: { uint16_t v=read_joy0(); if(g_inlog) inlog("JOY0DAT",v); return v; } /* JOY0DAT (mouse/port0) */
        case 0x00c: { uint16_t v=encode_joy(); if(g_inlog) inlog("JOY1DAT",v); return v; } /* JOY1DAT (joy/port1) */
        case 0x00e: return 0x0000;                             /* CLXDAT  */
        case 0x010: return g_custom[0x09e>>1];                 /* ADKCONR */
        case 0x016: { uint16_t v=read_potgor(); if(g_inlog) inlog("POTGOR",v); return v; } /* POTGOR (mouse/right btns) */
        case 0x01c: return g_custom[0x09a>>1];                 /* INTENAR */
        case 0x01e: return g_custom[0x09c>>1];                 /* INTREQR */
        default:    return g_custom[off>>1];
    }
}

/* ------------------------------------------------------------- OCS blitter */
/* Driven by g_custom[]; a blit is triggered by a write to BLTSIZE ($058).
 * Implemented synchronously (CPU >> Amiga, so instant is fine). Covers:
 *   - Area mode: USEA/USEB/USEC/USED, ASH/BSH barrel shifts across word
 *     boundaries, first/last-word masks (chan A), 256-entry minterm LF,
 *     signed per-row modulos, descending (DESC), and area fill (EFE/IFE/FCI).
 *   - Line mode: Bresenham single-pixel line draw (octant from BLTCON1).
 * After a blit: BBUSY (DMACONR bit14) reads 0 (instant), BZERO (bit13) set
 * iff every D word written was zero.
 *
 * Custom-register word offsets (indexed via g_custom[off>>1]):
 *   BLTCON0 040 BLTCON1 042 BLTAFWM 044 BLTALWM 046
 *   BLTCPT  048 BLTBPT  04c BLTAPT  050 BLTDPT  054  (H=hi word, L=lo word)
 *   BLTSIZE 058 BLTCMOD 060 BLTBMOD 062 BLTAMOD 064 BLTDMOD 066
 *   BLTCDAT 070 BLTBDAT 072 BLTADAT 074 BLTDDAT 000(write-only, unused)
 */
static uint64_t g_blit_count = 0;
static int g_bltlog = 0; static uint64_t g_bltlog_from = 0, g_bltlog_to = 0;  /* --bltlog FROM TO: log each blit's chan ptrs in an icount window */
static int g_cflog = 0;   /* --cflog: trace compose blits (dst in display/master region 0x68000..0x8a000) + flips (0x3fcd6), tagged by frame */
static int g_cur_frame;   /* fwd (defined below); used by the --cflog blit trace */

static inline uint16_t blt_r16(uint32_t a) {
    a &= (RAM_SIZE-1); return (uint16_t)((g_ram[a]<<8)|g_ram[a+1]);
}
static int sndcode_guard(uint32_t a, uint32_t v, int sz); /* fwd: sound-code write guard */
static inline void blt_w16(uint32_t a, uint16_t v) {
    a &= (RAM_SIZE-1);
    if (sndcode_guard(a, v, 2)) return;   /* drop a stray blit into live sound code */
    g_ram[a]=(uint8_t)(v>>8); g_ram[a+1]=(uint8_t)v;
}

static inline uint32_t blt_getptr(uint32_t hi_off) {
    return (((uint32_t)g_custom[hi_off>>1] << 16) | g_custom[(hi_off+2)>>1]) & 0x1ffffe;
}
static inline void blt_setptr(uint32_t hi_off, uint32_t p) {
    g_custom[hi_off>>1]     = (uint16_t)(p >> 16);
    g_custom[(hi_off+2)>>1] = (uint16_t)(p & 0xffff);
}

static void blitter_line(void);   /* fwd */

static void blitter_run(void) {
    uint16_t con0 = g_custom[0x040>>1];
    uint16_t con1 = g_custom[0x042>>1];
    g_blit_count++;

    if (con1 & 0x0001) { blitter_line(); return; }   /* LINE mode */

    /* ---- area (copy/cookie-cut/fill) ---- */
    uint16_t bltsize = g_custom[0x058>>1];
    int height = (bltsize >> 6) & 0x3ff; if (height==0) height = 1024;
    int width  =  bltsize       & 0x3f;  if (width ==0) width  = 64;

    int ash  = (con0 >> 12) & 0xf;
    int bsh  = (con1 >> 12) & 0xf;
    int usea = (con0 >> 11) & 1;
    int useb = (con0 >> 10) & 1;
    int usec = (con0 >>  9) & 1;
    int used = (con0 >>  8) & 1;
    uint8_t  lf   = con0 & 0xff;
    uint16_t fwm  = g_custom[0x044>>1];
    uint16_t lwm  = g_custom[0x046>>1];

    int desc = (con1 >> 1) & 1;
    /* fill: bit3=FCI(fill carry in) bit4=IFE(inclusive) bit5=EFE(exclusive) */
    int fci  = (con1 >> 3) & 1;
    int ife  = (con1 >> 4) & 1;
    int efe  = (con1 >> 5) & 1;
    int fill = ife | efe;

    uint32_t apt = blt_getptr(0x050);
    uint32_t bpt = blt_getptr(0x04c);
    uint32_t cpt = blt_getptr(0x048);
    uint32_t dpt = blt_getptr(0x054);
    int16_t  amod = (int16_t)g_custom[0x064>>1];
    int16_t  bmod = (int16_t)g_custom[0x062>>1];
    int16_t  cmod = (int16_t)g_custom[0x060>>1];
    int16_t  dmod = (int16_t)g_custom[0x066>>1];

    int step = desc ? -2 : 2;

    if (g_bltlog && g_log && g_icount>=g_bltlog_from && g_icount<=g_bltlog_to)
        fprintf(g_log, "BLIT ic=%llu con0=%04x con1=%04x %dx%d A=%06x B=%06x C=%06x D=%06x amod=%d cmod=%d dmod=%d pc=%06x\n",
            (unsigned long long)g_icount, con0, con1, width, height, apt, bpt, cpt, dpt, amod, cmod, dmod,
            (unsigned)m68k_get_reg(NULL,M68K_REG_PPC));

    /* COMPOSE/FLIP TRACE (--cflog): only blits whose dst lands in the display-buffer
     * pair (0x6bdfa/0x75a3c, +planes) or the compose master (0x80000) -- i.e. the
     * blits that build the visible screen.  Correlate with CF-FLIP lines (moon_instr_hook
     * @0x3fcd6) to see whether a transition's displayed half is filled before or after
     * the copper is flipped onto it. */
    if (g_cflog && g_log && dpt >= 0x68000u && dpt < 0x8a000u)
        fprintf(g_log, "CF-BLT fr=%d D=%06x A=%06x %dx%d con0=%04x con1=%04x lf=%02x amod=%d dmod=%d pc=%06x\n",
            g_cur_frame, dpt, apt, width, height, con0, con1, lf, amod, dmod,
            (unsigned)m68k_get_reg(NULL,M68K_REG_PPC));

    /* barrel-shift holdover words: the shift folds in the previous source word.
     * For ascending, shifting A right needs the OLD (higher-address) word's low
     * bits; for descending the geometry mirrors. We keep the prior fetched word
     * per channel and re-assemble a 32-bit window each step. */
    uint16_t aold = 0, bold = 0;

    uint32_t bzero = 0;   /* OR of every D word; BZERO set if this stays 0 */

    for (int y = 0; y < height; y++) {
        aold = 0; bold = 0;
        int fillcarry = fci;     /* reset fill carry at start of each row */
        for (int x = 0; x < width; x++) {
            /* ---- channel A ---- */
            uint16_t adat;
            if (usea) {
                adat = blt_r16(apt);
                if (x==0)        adat &= fwm;
                if (x==width-1)  adat &= lwm;
            } else {
                adat = g_custom[0x074>>1];   /* BLTADAT preload */
            }
            /* ---- channel B ---- */
            uint16_t bdat = useb ? blt_r16(bpt) : g_custom[0x072>>1];
            /* ---- channel C ---- */
            uint16_t cdat = usec ? blt_r16(cpt) : g_custom[0x070>>1];

            /* barrel shifts (A by ASH, B by BSH), 32-bit window with prev word */
            uint16_t ash_dat, bsh_dat;
            if (!desc) {
                uint32_t aw = ((uint32_t)aold << 16) | adat;
                ash_dat = (uint16_t)(aw >> ash);
                uint32_t bw = ((uint32_t)bold << 16) | bdat;
                bsh_dat = (uint16_t)(bw >> bsh);
            } else {
                /* descending: the 32-bit window folds the previous (already
                 * fetched, lower-address) word into the high half, shift left. */
                uint32_t aw = ((uint32_t)adat << 16) | aold;
                ash_dat = (uint16_t)(aw >> (16 - ash));
                uint32_t bw = ((uint32_t)bdat << 16) | bold;
                bsh_dat = (uint16_t)(bw >> (16 - bsh));
            }
            aold = adat;
            bold = bdat;

            /* ---- minterm: D = f(LF, A,B,C) bitwise ---- */
            uint16_t a = ash_dat, b = bsh_dat, c = cdat;
            uint16_t d = 0;
            /* For each output bit, index lf with (A<<2)|(B<<1)|C built per-bit. */
            uint16_t na=~a, nb=~b, nc=~c;
            if (lf & 0x01) d |= (uint16_t)(na & nb & nc);
            if (lf & 0x02) d |= (uint16_t)(na & nb &  c);
            if (lf & 0x04) d |= (uint16_t)(na &  b & nc);
            if (lf & 0x08) d |= (uint16_t)(na &  b &  c);
            if (lf & 0x10) d |= (uint16_t)( a & nb & nc);
            if (lf & 0x20) d |= (uint16_t)( a & nb &  c);
            if (lf & 0x40) d |= (uint16_t)( a &  b & nc);
            if (lf & 0x80) d |= (uint16_t)( a &  b &  c);

            /* ---- area fill (operates on D, MSB->LSB within each word, but the
             * blitter processes words right-to-left in fill; Moonstone fills use
             * DESC so words arrive in descending order already). Fill scans bit
             * by bit from LSB upward across the row. ---- */
            if (fill) {
                /* Area fill scans LSB->MSB within each word, carry threads
                 * across words in the row. IFE (inclusive): edge bits are part
                 * of the filled region (out = carry|in, toggle on set bit).
                 * EFE (exclusive): edge bits excluded (out = carry-before-edge,
                 * toggle on set bit). */
                uint16_t out = 0;
                for (int bit = 0; bit < 16; bit++) {
                    uint16_t m = (uint16_t)(1u << bit);
                    int inbit = (d & m) ? 1 : 0;
                    int outbit;
                    if (efe) {
                        outbit = fillcarry;          /* exclusive: pre-edge carry */
                        if (inbit) fillcarry ^= 1;
                    } else { /* ife */
                        if (inbit) fillcarry ^= 1;
                        outbit = fillcarry | inbit;  /* inclusive: include edges */
                    }
                    if (outbit) out |= m;
                }
                d = out;
            }

            if (used) blt_w16(dpt, d);
            bzero |= d;

            /* advance pointers one word */
            if (usea) apt += step;
            if (useb) bpt += step;
            if (usec) cpt += step;
            if (used) dpt += step;
        }
        /* end of row: apply modulos (sign of advance follows DESC) */
        if (usea) apt += desc ? -amod : amod;
        if (useb) bpt += desc ? -bmod : bmod;
        if (usec) cpt += desc ? -cmod : cmod;
        if (used) dpt += desc ? -dmod : dmod;
    }

    /* write back final pointers (real HW leaves them advanced) */
    blt_setptr(0x050, apt & 0x1ffffe);
    blt_setptr(0x04c, bpt & 0x1ffffe);
    blt_setptr(0x048, cpt & 0x1ffffe);
    blt_setptr(0x054, dpt & 0x1ffffe);

    /* BZERO (DMACONR bit13): set if every D word was zero */
    if (bzero == 0) g_custom[0x096>>1] |= 0x2000;
    else            g_custom[0x096>>1] &= ~0x2000;

    /* Assert BBUSY for the blit's modelled duration (see g_blt_busy_until). */
    g_blt_busy_until = g_cycles + (uint64_t)width * (uint64_t)height * (uint64_t)g_blt_cyc_per_word;
}

/* Bresenham line draw. BLTCON0/1 octant + masks per HRM "line mode".
 *  - BLTAPT holds the Bresenham accumulator (start = 2*dy - dx, sign in con1).
 *  - BLTCPT points at the first word of the line's start scanline.
 *  - BLTBMOD = 4*dy, BLTAMOD = 4*(dy-dx), texture in BLTBDAT (pattern, 0xffff=solid).
 *  - width field of BLTSIZE must be 2; height = number of pixels.
 *  - con1 bits: SUD(4) SUL(3) AUL(2) -> octant; SIGN(6) initial sign; bit0 LINE.
 *  - con0 bits 15-12 = start bit position (shift) of the first pixel in the word.
 */
static void blitter_line(void) {
    uint16_t con0 = g_custom[0x040>>1];
    uint16_t con1 = g_custom[0x042>>1];
    uint16_t bltsize = g_custom[0x058>>1];
    int length = (bltsize >> 6) & 0x3ff; if (length==0) length = 1024;

    int usea = (con0 >> 11) & 1; (void)usea;
    int usec = (con0 >>  9) & 1;
    int used = (con0 >>  8) & 1;
    uint8_t lf = con0 & 0xff;

    int sud = (con1 >> 4) & 1;   /* sometimes-up/down  */
    int sul = (con1 >> 3) & 1;   /* sometimes-up/left  */
    int aul = (con1 >> 2) & 1;   /* always-up/left     */
    int sign= (con1 >> 6) & 1;
    int single = (con1 >> 1) & 1; /* one-dot per row (anti single-pixel double) */

    int shift = (con0 >> 12) & 0xf;   /* x position of start pixel within word */

    uint32_t cpt = blt_getptr(0x048);     /* C = dest base */
    uint32_t dpt = used ? blt_getptr(0x054) : cpt;
    int16_t cmod = (int16_t)g_custom[0x060>>1];   /* bytes-per-row (line width) */

    /* Bresenham accumulator in BLTAPT low word (signed). */
    int16_t aacc = (int16_t)(blt_getptr(0x050) & 0xffff);
    int16_t amod = (int16_t)g_custom[0x064>>1];   /* 4*(dy-dx) */
    int16_t bmod = (int16_t)g_custom[0x062>>1];   /* 4*dy      */

    uint16_t texture = g_custom[0x072>>1];        /* BLTBDAT line pattern */
    int texrot = (con1 >> 12) & 0xf;              /* BSH = initial texture phase */
    uint16_t tex = (uint16_t)((texture << texrot) | (texture >> (16-texrot)));

    int curshift = shift;
    uint32_t addr = cpt;
    int started = single ? 0 : 0;
    (void)usec; (void)lf;

    for (int i = 0; i < length; i++) {
        /* plot one pixel if texture bit set */
        int texbit = (tex >> 15) & 1;
        tex = (uint16_t)((tex << 1) | texbit);   /* rotate texture */
        if (texbit && !(single && started)) {
            uint32_t wa = addr + (curshift >> 3 & ~1);
            wa = addr + ((curshift >> 4) * 2);
            uint16_t mask = (uint16_t)(0x8000 >> (curshift & 15));
            uint16_t cur = blt_r16(wa);
            /* USED minterm typically draws set pixels (LF=0x4a/0xca etc).
             * Treat as OR for set, but honor LF "invert" patterns minimally. */
            uint16_t nw;
            if (lf & 0x40) nw = cur | mask;         /* common: A&C term -> set  */
            else           nw = cur | mask;
            blt_w16(wa, nw);
        }
        if (single) started = 1;

        /* Bresenham step (Amiga octant decode) */
        if (aacc < 0) {
            aacc += bmod;
        } else {
            aacc += amod;
            /* move in the "sometimes" minor axis */
            if (sud) {
                /* x is major: step minor (y) */
                addr += sul ? -cmod : cmod;
            } else {
                /* y is major: step minor (x) */
                if (sul) { curshift--; if (curshift < 0) { curshift = 15; addr -= 2; } }
                else     { curshift++; if (curshift > 15){ curshift = 0;  addr += 2; } }
            }
            started = 0;
        }
        /* always-axis step */
        if (sud) {
            /* x major -> step x every pixel */
            if (aul) { curshift--; if (curshift < 0){ curshift=15; addr-=2; } }
            else     { curshift++; if (curshift > 15){ curshift=0; addr+=2; } }
        } else {
            /* y major -> step y every pixel */
            addr += aul ? -cmod : cmod;
        }
        (void)sign;
    }

    g_custom[0x096>>1] &= ~0x2000;   /* BZERO clear-ish after line */
    blt_setptr(0x054, dpt & 0x1ffffe);

    /* Assert BBUSY for the line's modelled duration (one word per pixel). */
    g_blt_busy_until = g_cycles + (uint64_t)length * (uint64_t)g_blt_cyc_per_word;
}

/* =========================== Amiga trackdisk DMA ===========================
 * `program` loads its title screen + the Mog engine from the floppy via RAW
 * trackdisk DMA (NOT dos.library / the HLE load-by-name path that served the
 * `program` binary itself).  Sequence it performs (verified, pc 0x298ea..0x29918):
 *   DSKPT  ($020/$022) <- chip-RAM buffer
 *   DSKSYNC($07e)       <- 0x4489          (one sync word)
 *   ADKCON ($09e)       <- 0x8400          (WORDSYNC)
 *   DMACON ($096)       <- 0x8210          (DMAEN | DSKEN)
 *   DSKLEN ($024)       <- 0x4000 then 0xa800 twice  (DMAEN | count words)
 *   wait at 0x29918 for the DSKBLK (INTREQ bit1) ISR to clear a busy flag.
 * Then it scans the buffer for sectors (`[a0+6]==0x4489`, `[a0+8]==0x55`) and
 * MFM-decodes each (decoder 0x29a88) into 0x220-byte records, keying off the
 * decoded sector number (record+6, must be 0..10 and unique).
 *
 * We serve this from the original decoded-AmigaDOS-sector ADF images.  The
 * drive head position is modelled from the game's CIA-B PRB ($BFD100) writes:
 *   bit0 = /STEP, bit1 = DIR (1 = step toward cyl 0), bit2 = /SIDE (1 = head0,
 *   0 = head1; per code at 0x29328), bits3-6 = /SEL0..3 (active low).
 * /TRK0 (CIA-A PRA bit4) is asserted (0) iff the modelled cylinder == 0 so the
 * recalibrate loop (0x29b44) terminates correctly.  The track read is then
 * ADF track = cyl*2 + head, MFM-encoded faithfully (the game has a standard
 * odd/even MFM decoder; we encode to match it exactly). */
#define NDRIVE 4
static uint8_t  *g_adf[3] = {0,0,0};    /* Disk1/2/3 images (901120 B each) */
static long      g_adfsz[3] = {0,0,0};
static int       g_disk_inserted = 0;   /* which ADF index is in drive 0 (default Disk1) */
static int       g_chng_low = 0;         /* /CHNG asserted low (disk just changed) until a step */
static int       g_dsk_cyl = 0;         /* modelled cylinder 0..79 (drive 0) */
static int       g_dsk_head = 0;        /* modelled side: 0 or 1 */
static int       g_dsk_sel = -1;        /* selected drive (0..3), -1 none */
static uint8_t   g_prb_prev = 0xff;     /* previous $BFD100 value (edge detect) */
static int       g_dsk_known = 0;       /* head position known (after recalibrate) */
static uint64_t  g_dsk_reads = 0;
/* --- seamless (automatic) disk swapping ----------------------------------
 * The game is a 3-floppy release.  On real hardware, at certain points it
 * prints "Please insert Disk B/C in Drive A — Press fire when done" and waits
 * for the player to physically swap the floppy and press fire.  For a
 * double-click-playable distributable we make this INVISIBLE: all three ADF
 * images are kept resident in RAM, and when the game asks for a particular
 * disk we instantly "insert" it (point the virtual drive at that ADF, pulse
 * /CHNG) and auto-confirm the fire so the prompt passes through unseen.
 *
 * Detection is faithful to the game's own code: the prompt-display routine
 * (program @0x23fea, also Mog-relocated copy) is called with a0 = a prompt
 * descriptor whose +2 word is the low 16 bits of the requested disk-name
 * string ("Please insert Disk A/B/C" @0x3024f/0x3023a/0x30264).  We map that
 * to the ADF index, swap, and arm an auto-fire that satisfies the fire-wait
 * at 0x22fd0 (which tests the decoded fire bit produced from CIA-A PRA bit7).
 * Disabled when the player drives swaps manually with --diskat. */
static int       g_autoswap = 1;        /* default: handle disk prompts automatically */
static int       g_autoswap_armed = 0;  /* a prompt is up; pulse fire to confirm it */
static uint64_t  g_autoswaps = 0;       /* count of automatic swaps performed */
static int       g_autoswap_settle = 0; /* frames to let the new disk settle before auto-fire */
/* --- floppy motor/seek busy-wait acceleration -----------------------------
 * The Mog engine's trackdisk driver paces each disk access with CIA-A TimerA
 * busy-waits that model the PHYSICAL floppy drive's motor spin-up / head-settle
 * time -- not data transfer.  The two waits are:
 *   - 0x3b7a0..0x3b7c2: a single TimerA underflow ($0864, ~3ms),
 *   - 0x3b882..0x3b8ce: a `dbra` loop of 25 x TimerA underflow ($8bd8, ~50ms ea)
 *     = ~1.26s PER CALL, called ~10x across the post-intro Disk1->Disk2 load.
 * On real hardware these waits are mandatory (the head can't read mid-seek).  We
 * serve the disk from files in RAM (no physical drive), so the motor/seek wait is
 * pure dead time -- it produces the bulk of the ~8s black at the intro->gameplay
 * hand-off.  When g_dsk_fastwait is set we short-circuit the two wait poll loops
 * (assert the TimerA-underflow ICR bit when the CPU polls it at 0x3b7b8/0x3b8bc)
 * so each delay collapses to ~one iteration.  The disk DMA reads, sector decode,
 * IRQ handshakes, and seek STEP modelling are all UNCHANGED -- only the artificial
 * motor/settle dead time is removed.  Scoped to the Mog driver (these PCs never run
 * during the attract intro, so the confirmed lightning/audio sync is untouched, and
 * they only run while a disk read is in progress, never in settled gameplay). */
static int       g_dsk_fastwait = 1;    /* 1 = skip the floppy motor/seek dead time (--noflopdelay disables) */
static int       g_skipat = 0;          /* --skipat N (diag): auto-trigger the intro-skip at host frame N (0=off) */
static int       g_stallat = 0;         /* --stallat N (diag): simulate a ~0.5s host stall (window drag) at host frame N (0=off) */
#define ADF_BYTES   901120              /* 80*2*11*512 */
#define ADF_TRACKS  160                 /* 80 cyl * 2 heads */
#define SEC_PER_TRK 11
#define MFM_SLOT    1088                /* MFM bytes per sector (decoder expects 0x440) */

/* Insert one bit's clock into an MFM half-word.  `mfmdata` holds the recovered
 * data bits in the 0x5555 positions (even bit indices); the 0xAAAA positions
 * (clock) start at 0.  Clock bit C just left of data bit D is set iff the
 * previous data bit and D are both 0 (standard MFM "no two consecutive 0s").
 * The game's decoder masks clock bits off, so they only matter for keeping the
 * stream MFM-legal and avoiding spurious 0x4489 sync matches. */
static uint16_t mfm_clock_fill(uint16_t mfmdata, int *prevbit) {
    uint16_t out = mfmdata;
    for (int i = 15; i >= 1; i -= 2) {
        int d = (mfmdata >> (i-1)) & 1;       /* data bit at even position i-1 */
        if (!*prevbit && !d) out |= (uint16_t)(1u << i);
        *prevbit = d;
    }
    return out;
}
/* Encode `len` plain bytes into MFM: odd half (len bytes) then even half (len
 * bytes), matching decoder 0x29a88: decoded = ((odd<<1)&0xAAAA)|(even&0x5555). */
static void mfm_encode_block(uint8_t *dst, const uint8_t *src, int len, int *pbOdd, int *pbEven) {
    for (int i = 0; i < len; i += 2) {
        uint16_t w = (uint16_t)((src[i]<<8) | src[i+1]);
        uint16_t odd = mfm_clock_fill((uint16_t)((w >> 1) & 0x5555), pbOdd);
        dst[i] = (uint8_t)(odd >> 8); dst[i+1] = (uint8_t)odd;
    }
    for (int i = 0; i < len; i += 2) {
        uint16_t w = (uint16_t)((src[i]<<8) | src[i+1]);
        uint16_t ev = mfm_clock_fill((uint16_t)(w & 0x5555), pbEven);
        dst[len+i] = (uint8_t)(ev >> 8); dst[len+i+1] = (uint8_t)ev;
    }
}

/* Build one MFM sector slot (1088 B) for (track, sector) from `data` (512 B).
 * Layout the decoder expects, indexed from the slot start a0:
 *   a0[0..3]   pre1   (mfm of {0,0}, decoded->record[0,1], unused)
 *   a0[4..7]   pre2   (a0[6,7] forced to literal sync 0x4489)
 *   a0[8..f]   info   long {0xFF, track, sector, 11-sector}  (a0[8],a0[c]==0x55)
 *   a0[10..2f] label  (16 B, zero)
 *   a0[30..37] header checksum (zero; game does not verify here)
 *   a0[38..3f] data checksum   (zero)
 *   a0[40..43f] data (512 B) */
static void mfm_build_sector(uint8_t *slot, int track, int sector, const uint8_t *data) {
    uint8_t info[4]  = { 0xFF, (uint8_t)track, (uint8_t)sector, (uint8_t)(SEC_PER_TRK - sector) };
    uint8_t zero2[2] = {0,0}, label[16] = {0}, hck[4] = {0}, dck[4] = {0};
    int pbO = 0, pbE = 0, p = 0;
    memset(slot, 0, MFM_SLOT);
    mfm_encode_block(slot+p, zero2, 2,  &pbO, &pbE); p += 4;    /* pre1  a0[0..3]  */
    mfm_encode_block(slot+p, zero2, 2,  &pbO, &pbE); p += 4;    /* pre2  a0[4..7]  */
    mfm_encode_block(slot+p, info,  4,  &pbO, &pbE); p += 8;    /* info  a0[8..f]  */
    mfm_encode_block(slot+p, label, 16, &pbO, &pbE); p += 32;   /* label a0[10..2f]*/
    mfm_encode_block(slot+p, hck,   4,  &pbO, &pbE); p += 8;    /* hdr ck a0[30..37]*/
    mfm_encode_block(slot+p, dck,   4,  &pbO, &pbE); p += 8;    /* dat ck a0[38..3f]*/
    mfm_encode_block(slot+p, data,  512,&pbO, &pbE); p += 1024; /* data  a0[40..43f]*/
    slot[6] = 0x44; slot[7] = 0x89;     /* literal sync the scan keys on (a0[6,7]) */
}

/* Perform a trackdisk DMA read: copy `words` MFM words for the current
 * (cyl,head) into chip RAM at `dest`, then raise DSKBLK. */
static void trackdisk_dma(uint32_t dest, int words) {
    /* The cylinder is modelled faithfully from the CIA-B /STEP pulses (and drives
     * /TRK0); the side is the live $BFD100 bit2 at DMA time (what the drive would
     * physically read).  The game's own cyl global [0x7ffee] is used to correct
     * any step-count drift in the cylinder (the head positioner is exact, but
     * being defensive); the modelled cylinder is cross-checked and matches it. */
    int cyl  = g_dsk_cyl, head = g_dsk_head;
    int gcyl = (int)(int16_t)r16(0x7ffee);
    if (gcyl >= 0 && gcyl <= 79) cyl = gcyl;  /* authoritative cylinder */
    int track = cyl * 2 + head;
    uint8_t *adf = g_adf[g_disk_inserted];
    long adfsz = g_adfsz[g_disk_inserted];
    g_dsk_reads++;
    if (!adf || track < 0 || track >= ADF_TRACKS) {
        if (g_log) fprintf(g_log, "TRACKDISK read FAIL cyl=%d head=%d track=%d (no adf/oob)\n", cyl, head, track);
    } else {
        /* assemble the 11-sector MFM track in a temp buffer, then copy the
         * requested word count into chip RAM at `dest`. */
        static uint8_t mfmtrk[SEC_PER_TRK * MFM_SLOT];
        long base = (long)track * SEC_PER_TRK * 512;
        for (int s = 0; s < SEC_PER_TRK; s++) {
            const uint8_t *secdata = (base + (long)s*512 + 512 <= adfsz) ? (adf + base + (long)s*512) : NULL;
            uint8_t zerod[512] = {0};
            mfm_build_sector(mfmtrk + s*MFM_SLOT, track, s, secdata ? secdata : zerod);
        }
        /* The first stored word must be the sync 0x4489 (WORDSYNC hardware
         * behaviour): the game sanity-checks `[DSKPT]==0x4489` at 0x2992e before
         * decoding.  This word is also sector 0's pre-amble, which decodes to a
         * throwaway record longword, so overwriting it is harmless. */
        mfmtrk[0] = 0x44; mfmtrk[1] = 0x89;
        uint32_t nbytes = (uint32_t)words * 2;
        if (nbytes > sizeof(mfmtrk)) nbytes = sizeof(mfmtrk);
        for (uint32_t i = 0; i < nbytes; i++) w8(dest + i, mfmtrk[i]);
        if (g_log) fprintf(g_log, "TRACKDISK read #%llu cyl=%d head=%d track=%d disk=%d sec=%d -> dest=%06x words=%d (%u B)\n",
                (unsigned long long)g_dsk_reads, cyl, head, track, g_disk_inserted+1,
                (int)(int16_t)r16(0x7ffec), dest, words, nbytes);
    }
    /* raise DSKBLK (INTREQ bit1, level 1) so the disk ISR runs and clears the
     * game's busy flag; clear DMAEN so a re-read needs a fresh DSKLEN write. */
    g_custom[0x09c>>1] |= 0x0002;
    update_ipl();
}

/* Directory to load the three ADF images from (the bundled `data/` folder, set
 * exe-relative in main(); overridable with --diskdir).  Empty = search the dev
 * fallback paths only. */
static const char *g_diskdir = "";

/* Load the three decoded-sector ADF images.  Search order per disk:
 *   1. <g_diskdir>/<full name>   (the bundled `data/` folder, exe-relative)
 *   2. <g_diskdir>/Disk<N>.adf   (short name, if the bundle was renamed)
 *   3. dev fallbacks ../, ../../, ./, ""  (running from recomp/ or repo root)
 * so the same binary works both from the dev tree and from the distributable. */
static void disk_load_adfs(void) {
    static const char *names[3] = {
        "Moonstone - A Hard Days Knight_Disk1.adf",
        "Moonstone - A Hard Days Knight_Disk2.adf",
        "Moonstone - A Hard Days Knight_Disk3.adf",
    };
    static const char *shortn[3] = { "Disk1.adf", "Disk2.adf", "Disk3.adf" };
    static const char *prefixes[] = { "../", "../../", "./", "" };
    for (int i = 0; i < 3; i++) {
        char cands[8][1200]; int nc = 0;
        if (g_diskdir && *g_diskdir) {
            snprintf(cands[nc++], 1200, "%s/%s", g_diskdir, names[i]);
            snprintf(cands[nc++], 1200, "%s/%s", g_diskdir, shortn[i]);
        }
        for (int p = 0; p < (int)(sizeof(prefixes)/sizeof(prefixes[0])); p++)
            snprintf(cands[nc++], 1200, "%s%s", prefixes[p], names[i]);
        for (int c = 0; c < nc; c++) {
            FILE *f = fopen(cands[c], "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            g_adf[i] = (uint8_t*)malloc(sz);
            g_adfsz[i] = (long)fread(g_adf[i], 1, sz, f);
            fclose(f);
            if (g_log) fprintf(g_log, "DISK%d loaded %s (%ld B)\n", i+1, cands[c], g_adfsz[i]);
            break;
        }
        if (!g_adf[i] && g_log) fprintf(g_log, "DISK%d NOT FOUND (%s)\n", i+1, names[i]);
    }
    g_disk_inserted = 0;   /* Disk1 in drive 0 by default */
}

/* ===== AmigaDOS OFS reader: extract boot modules from a player-supplied ADF =====
 * The Moonstone floppies are standard OFS ("DOS\0") disks, so the four boot
 * modules (nb, program, mog, crystal) are ordinary files on Disk 1.  Rather than
 * make players extract those by hand, we read them straight out of the .adf
 * images they already supply.  Returns a malloc'd buffer (caller frees) or NULL.
 * Case-insensitive (AmigaDOS is).  OFS only (these disks are OFS, not FFS). */
static uint32_t ofs_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint8_t *adf_ofs_extract(const uint8_t *adf, long sz, const char *name, long *outlen) {
    const long BS = 512;
    if (sz < 882*BS || memcmp(adf, "DOS", 3) != 0 || (adf[3] & 1)) return NULL;
    long nblk = sz / BS;
    const uint8_t *rb = adf + 880*BS;                 /* DD root block */
    long stk[2048]; int sp = 0, namelen = (int)strlen(name);
    for (int i = 0; i < 72; i++) { uint32_t e = ofs_be32(rb+24+i*4); if (e && sp < 2048) stk[sp++] = e; }
    long found = -1;
    while (sp > 0 && found < 0) {
        long b = stk[--sp];
        if (b < 2 || b >= nblk) continue;
        const uint8_t *hb = adf + b*BS;
        uint32_t nxt = ofs_be32(hb + BS-16);          /* hash chain */
        if (nxt && sp < 2046) stk[sp++] = nxt;
        if ((int32_t)ofs_be32(hb + BS-4) != -3) continue;   /* ST_FILE only */
        int nl = hb[BS-80];
        if (nl != namelen) continue;
        int eq = 1;
        for (int k = 0; k < nl; k++) {
            int a = hb[BS-79+k], c = (unsigned char)name[k];
            if (a>='A'&&a<='Z') a += 32;  if (c>='A'&&c<='Z') c += 32;
            if (a != c) { eq = 0; break; }
        }
        if (eq) found = b;
    }
    if (found < 0) return NULL;
    const uint8_t *hb = adf + found*BS;
    long fsize = (long)ofs_be32(hb + BS-188);          /* byte_size @ 0x144 */
    if (fsize < 0 || fsize > sz) return NULL;
    uint8_t *out = (uint8_t*)malloc(fsize > 0 ? fsize : 1);
    if (!out) return NULL;
    long got = 0; uint32_t nd = ofs_be32(hb + 16); int guard = 0;   /* first_data block */
    while (nd && nd < (uint32_t)nblk && got < fsize && guard < 200000) {
        const uint8_t *db = adf + (long)nd*BS;
        long dlen = (long)ofs_be32(db + 12);           /* data_size in this block */
        uint32_t next = ofs_be32(db + 16);             /* next_data block */
        if (dlen < 0 || dlen > 488) dlen = 488;
        if (got + dlen > fsize) dlen = fsize - got;
        memcpy(out + got, db + 24, dlen);
        got += dlen; nd = next; guard++;
    }
    *outlen = got;
    return out;
}

/* If the four boot modules aren't present as files in `datadir`, extract them
 * from the .adf images in `adfdir` (Disk 1 holds all four) and write them there.
 * One-time, on first launch; afterwards the files exist and this is a no-op.  So
 * a player only has to drop in the three .adf disk images. */
static void ensure_boot_modules(const char *datadir, const char *adfdir) {
    static const char *mods[4] = { "nb", "program", "mog", "crystal" };
    static const char *full[3] = {
        "Moonstone - A Hard Days Knight_Disk1.adf",
        "Moonstone - A Hard Days Knight_Disk2.adf",
        "Moonstone - A Hard Days Knight_Disk3.adf" };
    static const char *shrt[3] = { "Disk1.adf", "Disk2.adf", "Disk3.adf" };
    int need = 0;
    for (int m = 0; m < 4; m++) {
        char p[1300]; snprintf(p, sizeof(p), "%s/%s", datadir, mods[m]);
        FILE *f = fopen(p, "rb"); if (f) fclose(f); else need = 1;
    }
    if (!need) return;
    for (int d = 0; d < 3; d++) {
        char ap[1300]; FILE *af = NULL;
        snprintf(ap, sizeof(ap), "%s/%s", adfdir, full[d]); af = fopen(ap, "rb");
        if (!af) { snprintf(ap, sizeof(ap), "%s/%s", adfdir, shrt[d]); af = fopen(ap, "rb"); }
        if (!af) continue;
        fseek(af, 0, SEEK_END); long sz = ftell(af); fseek(af, 0, SEEK_SET);
        if (sz <= 0) { fclose(af); continue; }
        uint8_t *adf = (uint8_t*)malloc(sz);
        if (!adf) { fclose(af); continue; }
        sz = (long)fread(adf, 1, sz, af); fclose(af);
        for (int m = 0; m < 4; m++) {
            char p[1300]; snprintf(p, sizeof(p), "%s/%s", datadir, mods[m]);
            FILE *chk = fopen(p, "rb"); if (chk) { fclose(chk); continue; }  /* already have it */
            long olen = 0; uint8_t *bytes = adf_ofs_extract(adf, sz, mods[m], &olen);
            if (bytes) {
                if (olen > 0) {
                    FILE *of = fopen(p, "wb");
                    if (of) { fwrite(bytes, 1, olen, of); fclose(of);
                              if (g_log) fprintf(g_log, "extracted boot module '%s' (%ld B) from %s\n", mods[m], olen, ap); }
                    else if (g_log) fprintf(g_log, "WARN: cannot write '%s' (is the data folder read-only?)\n", p);
                }
                free(bytes);
            }
        }
        free(adf);
    }
}

/* ======================================================= Paula 4-ch audio */
/* Per-channel DMA-driven sample playback, mixed to a 44100 Hz S16 stereo host
 * stream.  Faithful to OCS Paula: a channel reads signed 8-bit samples from its
 * location pointer (two per word) at one sample every AUDxPER ticks of the PAL
 * audio clock (3546895 Hz).  When AUDxLEN words have been consumed it reloads
 * the location pointer & length from the latched AUDxLCH/AUDxLEN registers (for
 * looping / next-buffer) and raises the AUDx interrupt (INTREQ bit 7+N, level
 * 4) so the music player's ISR queues the next note/sample.
 *
 * The mixer is driven in lock-step with CPU time: each emulated PAL frame
 * generates exactly HOST_RATE/50 = 882 stereo output samples.  The audio clock
 * is the colour clock = CPU/2; per output sample each enabled channel advances
 * its sub-sample position by (channel_rate / HOST_RATE), where channel_rate =
 * PAULA_CLK / period.  Stereo map: ch0+ch3 = LEFT, ch1+ch2 = RIGHT.
 *
 * State machine mirrors hardware so IRQs fire at the right cadence even when the
 * game never reads our output: enabling a channel via DMACON latches LCH/LEN and
 * loads the first word; every period-tick advances byte-by-byte through the
 * current word, then word-by-word; on length exhaustion it reloads + raises the
 * AUDx INTREQ. */
#define PAULA_CLK     3546895u          /* PAL Paula sample clock (colour clock) */
#define HOST_RATE     44100
#define SMP_PER_FRAME (HOST_RATE/50)    /* 882 stereo frames per PAL video frame */
/* ISR-driven deferred-reload safety cap, in channel-ticks (see paula_tick_one). */
#define RELOAD_ISR_WAIT_MAX 64
/* SFX one-shot double-guard cap, in channel-ticks (see paula_tick_one): how long to
 * wait for the sequencer's silent-loop install before committing a same-sample reload.
 * ~300 ticks covers a VERTB-paced (once/frame) install; the hold emits clean silence. */
#define RELOAD_SFX_WAIT_MAX 300

typedef struct {
    /* live DMA state */
    uint32_t loc;          /* current read pointer into chip RAM (byte addr)   */
    uint16_t len;          /* words remaining in the current buffer            */
    int8_t   sample;       /* current 8-bit sample being held on the DAC       */
    int      wordhalf;     /* 0 = high byte next, 1 = low byte next            */
    int      first;        /* DMA just (re)started: refetch ptr/len from regs  */
    int      enabled;      /* AUDxEN (DMACON bit) AND master DMAEN             */
    int      reload_pending;/* buffer underflowed: reload loc/len from regs on the
                            * NEXT tick (models Paula's ~1-word reload latency, so
                            * the level-4 AUDx ISR that the underflow IRQ triggers
                            * can repoint AUDxLC/LEN before the next buffer commits
                            * -- the standard one-shot+silent-loop idiom).        */
    int      reload_isr_wait;/* the underflow that armed reload_pending happened on
                            * an ISR-DRIVEN channel (AUD IRQ enabled in INTENA): the
                            * silent loop is installed by the level-4 AUDx ISR, which
                            * runs a few CPU bursts later.  Hold the deferred reload
                            * (don't commit a buffer yet) until that ISR ACKs the IRQ
                            * (clears the INTREQ AUDx bit) -- so we never commit the
                            * still-real previous sample before the ISR silences it
                            * (would replay the SFX a 2nd time when the ISR runs late,
                            * past a single tick).  Bounded by reload_wait_ticks.    */
    int      reload_wait_ticks;/* channel-ticks the ISR-wait has held; bounded fallback:
                            * if the ISR never ACKs within ~a word-period, commit the
                            * reload anyway (matches HW looping the registers when the
                            * interrupt is genuinely masked -- never hang silent).    */
    /* host-rate resampling accumulator (fixed point, 16.16) */
    uint32_t acc;          /* fraction of one channel-sample accumulated       */
    uint32_t step;         /* channel-samples per host-sample, 16.16           */
    int      attack;       /* host-samples since this channel last (re)started; the
                            * de-click ramp fades gain 0->full over g_attack_ramp of
                            * them so a note ONSET isn't a razor step (a "tick").    */
} PaulaCh;

static PaulaCh g_paula[4];
static int     g_audio_on = 0;           /* SDL audio (or WAV) generation active */
static int     g_audio_paused = 0;       /* 1 = SDL device held paused (playing the primed silence) until the first
                                          * audible frame, so the prime cushion isn't drained empty during the load
                                          * (the intro-tick underrun fix); cleared in audio_flush. */
static int     g_audio_mute = 0;         /* 1 = don't push to the SDL queue (used during intro skip FF) */
/* Note-attack de-click ramp: a freshly (re)started channel's onset is a near-instant
 * step (0 -> note level); even through the output low-pass that reads as a faint
 * "tick" on each note.  Ramp the channel's gain 0->full over this many HOST samples
 * to round only the ONSET, leaving the sustained tone fully bright (unlike lowering
 * the LPF cutoff, which dulls everything).  32 ~= 0.7 ms -- well under the attack's
 * own perceptual rise, so notes stay punchy while the tick is gone.  0 = off.
 * --attackramp N (set in main).  Output-only (CPU-trace neutral).  DEFAULT OFF: it
 * only helps SFX that attack right at the DMA re-trigger; the intro "tick" is a
 * transient buried in the SAMPLE data (370 samples past the re-trigger), so the ramp
 * can't align to it -- the LED Butterworth handles that.  Kept as a tool. */
static int     g_attack_ramp = 0;

/* Sample reconstruction.  Paula (and our model) is a ZERO-ORDER HOLD: each 8-bit
 * sample is a flat step held until the next, so a fast attack is a hard STAIRCASE
 * whose ~3-host-sample steps (236-299 each during the intro boom's attack) read as a
 * "tick".  A real Amiga's analog output rounds those steps off.  g_interp=1 linearly
 * interpolates toward the next channel-sample (proper anti-imaging reconstruction):
 * the staircase becomes smooth ramps.  DEFAULT OFF -- it BACKFIRED (operator 2026-06-24:
 * "ticking is louder, now in the menu music too"): the peek `g_ram[loc+wordhalf]` reads
 * the WRONG byte at a sample loop/reload boundary (it sees the reload target, not the
 * true next sample), glitching on every loop.  Looping music (menu/loading = short
 * looped samples) hits those boundaries constantly -> ticks everywhere.  Would need a
 * boundary-aware peek to be safe.  --interp 1 to re-enable.  Output-only. */
static int     g_interp = 0;
static int     g_solo = -1;            /* --solo N: mute all Paula channels except N (diag: isolate which channel carries an artifact) */
static int     g_mute_mask = 0;        /* --mute N: mute channel N (bitmask); play the rest in context to ear-locate an artifact */
static int     g_aud1lim_thr = 0;      /* --aud1lim <thr>: per-channel attack-limiter threshold on AUD1 (s=samp*vol units; 0=off) */
static int     g_aud1lim_ratio = 4;    /* --aud1ratio <r>: downward-compression ratio above the threshold */
static double  g_aud1lim_env = 0.0;    /* AUD1 limiter peak-follower envelope state */
static int     g_boomlp_a = 0;         /* --boomlp <Hz>: per-channel de-click LP on the AUD2 bass "boom" (0=off). alpha<<16, 2-pole cascade. */
static int32_t g_boomlp_y1 = 0, g_boomlp_y2 = 0;  /* AUD2 boom-LP 2-pole cascade state */
static int     g_audstat = 0;          /* --audstat: log per-channel vol/per on change (diag: check for an abnormal channel volume) */
static int     g_audstat_pv[4]   = {-1,-1,-1,-1};  /* last logged vol per ch */
static int     g_audstat_pp[4]   = {-1,-1,-1,-1};  /* last logged per per ch */
/* The ISR-wait deferred-reload fix (faithful one-shot+silent-loop on ISR-driven
 * channels; see paula_tick_one).  ON by default -- the shipping build always has
 * the fix.  The off-switch (--noisrwait) exists only as an A/B audio-debug lever;
 * it is never needed in normal play. */
static int     g_isr_wait = 1;
/* SFX one-shot double-guard (see paula_tick_one): hold a same-sample SFX-bank reload
 * briefly for the sequencer's silent-loop install, so a one-shot grunt/death plays
 * once instead of a 2nd pass.  ON by default; --nosfxdblguard A/B-disables it. */
static int     g_sfx_dblguard = 1;
static int     g_dblguard_log = 0;       /* bounded SFX-DBLGUARD outcome log count */
static int     g_trumpetmode = 0;        /* intro trumpet "dun dun boom boom".  ROOT CAUSE FOUND 2026-06-24: the
                                          * song genuinely plays a 2nd trumpet voice 3 rows (~18 frames) after the
                                          * lead -- but at a deliberately QUIET level (lead C38=56, echo C10=16).
                                          * The music driver sets that level with a *byte* write to the even AUDxVOL
                                          * address (0xa04: move.b vol,($8,A5)); our m68k_write_memory_8 only wrote
                                          * the high byte, leaving the volume bits at the instrument default (full),
                                          * so the soft echo blasted at full = the audible doubling.  Fixed properly
                                          * by modelling the 68000 byte-write data duplication for AUDxVOL (see
                                          * m68k_write_memory_8).  So mode 0 (default) is now FAITHFUL: lead + the
                                          * soft tail, which masks like real hardware.  Diagnostic overrides kept:
                                          * mode 1 = STRIP AUD3's 0x0b trumpet (force fully single, gain-ramped);
                                          * mode 2 = sync AUD3->AUD0 unison.  --trumpetmode N.  Output-only. */
/* register activity logging (gated; bounded) */
static int     g_audlog = 0;             /* --audlog: log Paula register/DMA/IRQ activity */
static int     g_delvlog = 0;            /* --delvlog: TEMP diag -- trace moonstone-delivery altar handler + palette writes (declared early; used in custom_write) */
static uint32_t g_audlog_n = 0;
static int      g_screech_n = 0;         /* always-on SCREECH-WATCH line count (bounded) */
static int      g_cutoff_n  = 0;         /* always-on CUT-OFF watch line count (bounded) */
static int      g_sdl_mode  = 0;         /* 1 = live SDL play (show crash dialogs); 0 = headless (log only) */
static int      g_play_len0[4] = {0,0,0,0};  /* sample length (words) at the last ENABLE, per channel */
static uint32_t g_play_loc0[4] = {0,0,0,0};  /* sample start (loc) at the last ENABLE, per channel    */
static int      g_first_pass[4] = {0,0,0,0}; /* 1 = channel enabled and has NOT underflowed yet */
static int      g_len_armed[4]  = {0,0,0,0}; /* 1 = AUDxLEN has been (re)written since the last underflow.
                                              * The deferred reload must wait for this before latching loc+len,
                                              * else a reload firing mid-silent-loop-install latches the silent
                                              * buffer addr (0xc2a4) with the OLD length -> loud low-RAM garbage
                                              * = the combat "screech" (root-caused 2026-06-25). NOT in PaulaCh
                                              * (a save-struct field would re-break save-version compat). */
static int      g_double_n  = 0;             /* always-on REPLAY/DOUBLE watch line count (bounded) */
static int      g_screechrel_n = 0;          /* SCREECH-RELOAD (swap-path screech) line count (bounded) */
static uint64_t g_lastscreech_ic[4] = {0};   /* per-channel throttle for SCREECH-RELOAD */
static uint32_t g_last_sfx_loc[8] = {0};     /* ring of recent SFX-bank ENABLE locs (RETRIGGER watch) */
static uint64_t g_last_sfx_ic[8]  = {0};
static int      g_retrig_n  = 0;             /* RETRIGGER watch line count (bounded) */
/* COMBAT AUDIO TRACE (signature-agnostic screech hunt, 2026-06-25): the operator's
 * "high pitch" combat screech was NOT caught by SCREECH-WATCH/RELOAD -- both gate on a
 * LOUD+LONG sample (vol>=48 && len>=0x800), and the screech (ic 213-217M in the
 * 06-25 capture) slipped through (short and/or pitch-shifted).  This logs EVERY audible
 * sample CHANGE in combat (len/pitch-independent), so the next occurrence's
 * loc/len/per/vol/pc is captured no matter its signature.  Throttle is per-SAMPLE: a
 * distinct loc always logs immediately; only same-loc repeats are rate-limited, so a
 * looping ambience can't crowd out the anomaly.  Read-only -> determinism-neutral
 * (A/B verified 2026-06-25: dist/data icount=70083750 identical with/without). */
static int      g_audtrace_n = 0;            /* combat AUDTRACE line count (bounded)        */
static uint32_t g_audtrace_loc[4] = {0};     /* per-ch last-traced sample loc                */
static uint64_t g_audtrace_ic[4]  = {0};     /* per-ch last-traced ic (same-sample throttle) */
/* OUTPUT-LEVEL BEEP/SCREECH DETECTOR (2026-06-25): the operator's combat "clang-beep" is
 * a sustained HIGH-FREQUENCY tone that NO enable/reload-edge watch can see (our model
 * reloads exactly at len; the beep is whatever a LIVE channel is OUTPUTTING).  Estimate
 * each channel's fundamental from its zero-crossing rate over a ~5ms window; if a channel
 * SUSTAINS a high pitch while audible in combat, log loc/len/per so a WAV capture can be
 * correlated to the offending sample.  Read-only (reads the already-computed DAC value). */
static int      g_beepout_n = 0;                   /* BEEP-OUT line count (bounded)         */
static int      g_bo_prevsign[4]  = {0,0,0,0};     /* per-ch last definite output-sample sign */
static int      g_bo_cross[4]     = {0,0,0,0};     /* per-ch zero-crossings this window     */
static int      g_bo_win[4]       = {0,0,0,0};     /* per-ch samples counted this window    */
static int      g_bo_sustain[4]   = {0,0,0,0};     /* per-ch consecutive high-freq windows  */
static uint64_t g_bo_lastlog_ic[4]= {0,0,0,0};     /* per-ch log throttle                   */
static int      g_cur_frame;             /* fwd: current host frame (defined below) */
static uint64_t g_aud_irq[4] = {0,0,0,0};/* per-channel IRQs raised (stats) */

/* recompute a channel's host-rate step from its current period */
static void paula_recalc_step(int ch) {
    uint16_t per = g_custom[(0x0a6 + ch*0x10)>>1] & 0xffff;
    if (per < 1) per = 1;                /* avoid div-by-0 / runaway */
    double rate = (double)PAULA_CLK / (double)per;     /* samples/sec from RAM */
    double step = rate / (double)HOST_RATE;            /* channel-samples per host-sample */
    if (step > 256.0) step = 256.0;      /* clamp absurd periods */
    g_paula[ch].step = (uint32_t)(step * 65536.0 + 0.5);
}

/* latch location pointer + length from the registers (DMA (re)start / loop) */
static void paula_latch(int ch) {
    uint32_t b = 0x0a0 + ch*0x10;
    g_paula[ch].loc = (((uint32_t)g_custom[b>>1] << 16) | g_custom[(b+2)>>1]) & (RAM_SIZE-2);
    g_paula[ch].len = g_custom[(b+4)>>1];
    if (g_paula[ch].len == 0) g_paula[ch].len = 1;     /* HW plays 1 word for LEN=0 */
    g_paula[ch].wordhalf = 0;
    g_paula[ch].first = 0;
}

/* advance one channel by one Paula sample-tick: emit the held sample, then fetch
 * the next byte; on word/buffer boundaries reload + raise the AUDx interrupt */
static void paula_tick_one(int ch) {
    PaulaCh *c = &g_paula[ch];
    if (c->first) paula_latch(ch);
    /* Deferred reload: the previous tick's buffer underflowed and raised the AUDx
     * IRQ, but we postponed reading AUDxLC/AUDxLEN by one tick so the level-4 AUDx
     * ISR (driven by that IRQ, serviced by the interleaved CPU burst) can repoint
     * AUDxLC/LEN to its silent loop BEFORE we commit the next buffer.  Now read the
     * (possibly ISR-updated) registers.  This models Paula's ~1-word reload latency
     * and is what makes the standard one-shot+silent-loop idiom play exactly once
     * instead of twice. */
    if (c->reload_pending) {
        if (c->reload_isr_wait && g_isr_wait) {
            /* ISR-driven one-shot: the level-4 AUDx ISR (driven by the underflow
             * IRQ we raised) installs the 1-word silent loop, but it runs a few CPU
             * bursts later -- and because our CPU executes in 160-instr bursts with
             * audio interleaved BETWEEN them, the next channel-tick can fall in the
             * SAME audio batch as the underflow (no CPU burst yet, or L4 transiently
             * masked by SR), so a fixed 1-tick defer would commit the STILL-REAL
             * previous buffer and replay the sample a 2nd time.  Instead, hold here
             * (keep the held DAC sample -- Paula holds its last value) until the ISR
             * actually ACKs the IRQ, i.e. clears the INTREQ AUDx bit (0x80<<ch).
             * Then the deferred reload picks up the silent loop the ISR set, so the
             * SFX plays exactly once.  On real Paula the L4 ISR is serviced in a few
             * us (well under a word-period), so this wait is normally a single tick. */
            uint16_t pend = g_custom[0x09c>>1] & (uint16_t)(0x0080 << ch);
            /* SCREECH FIX (2026-06-25): also hold until the ISR has (re)written AUDxLEN.
             * The sound server installs the silent loop non-atomically (LCH/LCL THEN
             * LEN); releasing on the IRQ-ACK (pend clear) alone latched loc=0xc2a4 with
             * the PREVIOUS sample's LEN (1864 words) -> a loud burst of low-RAM garbage.
             * Waiting for g_len_armed makes loc+len latch coherently.  Same tick cap. */
            /* Bounded fallback: if the ISR never ACKs within RELOAD_ISR_WAIT_MAX
             * channel-ticks, commit anyway -- matches HW, which reloads AUDxLC/LEN
             * from the registers every word when the interrupt is genuinely masked,
             * so a never-serviced channel loops its buffer rather than hanging silent.
             * In normal play the L4 ISR ACKs within ~1 tick (the next CPU burst); the
             * cap only ever bites a pathological never-serviced channel, where the
             * worst case is a few ms of held-DAC silence (imperceptible) instead of a
             * spurious sample replay.  64 ticks ~= 6ms @ PER=339 -- generous headroom
             * over any realistic SR-masked L4 latency, still firmly bounded. */
            if ((pend || !g_len_armed[ch]) && c->reload_wait_ticks < RELOAD_ISR_WAIT_MAX) {
                c->reload_wait_ticks++;
                return;                 /* hold: don't fetch/advance this tick */
            }
            c->reload_isr_wait = 0;
            c->reload_wait_ticks = 0;
        }
        /* SFX ONE-SHOT DOUBLE-GUARD: about to commit the deferred reload.  If it would
         * re-commit the IDENTICAL SFX-bank sample (loc<0x0a0000) that just played, the
         * sequencer's silent-loop install (AUDxLC=0xc2a4 / AUDxLEN<=1) hasn't landed yet
         * -> hold (emitting clean silence) up to RELOAD_SFX_WAIT_MAX ticks for it, so a
         * one-shot grunt/death plays ONCE instead of a 2nd pass.  Gated to the SFX bank
         * so music instruments (0x0axxxx+, which legitimately loop the same buffer) are
         * NEVER held; only fires on an exact same-sample replay, so SFX that already go
         * silent are untouched; bound-expiry commits the replay (no worse than before). */
        if (g_sfx_dblguard) {
            uint32_t b  = 0x0a0 + ch*0x10;
            uint32_t rl = (((uint32_t)g_custom[b>>1]<<16)|g_custom[(b+2)>>1]) & (RAM_SIZE-2);
            uint16_t rn = g_custom[(b+4)>>1];
            int replay  = (rn > 1 && rl >= 0x080000 && rl < 0x0a0000 &&  /* SFX bank only (08xxxx/09xxxx),
                                                                          * NOT low-RAM engine buffers e.g. 0xc2a4 */
                           rl == g_play_loc0[ch] && (int)rn == g_play_len0[ch]);
            if (replay && c->reload_wait_ticks < RELOAD_SFX_WAIT_MAX) {
                c->reload_wait_ticks++;
                c->sample = 0;          /* clean silence while we wait for the silent-loop install */
                return;                 /* hold: don't fetch/advance this tick */
            }
            if (c->reload_wait_ticks > 0 && g_log && g_dblguard_log < 200) {
                fprintf(g_log, "SFX-DBLGUARD AUD%d %s after %d ticks loc=%06x ic=%llu\n",
                        ch, replay ? "GAVE-UP(double)" : "silenced", c->reload_wait_ticks,
                        (unsigned)g_play_loc0[ch], (unsigned long long)g_icount);
                fflush(g_log); g_dblguard_log++;
            }
            c->reload_wait_ticks = 0;
        }
        c->reload_pending = 0;
        paula_latch(ch);
        /* ALWAYS-ON REPLAY/DOUBLE WATCH: the deferred reload just committed a buffer.
         * If it's the SAME short SFX that just finished (same start + full length, and
         * len>1 so NOT the silent loop), the one-shot is about to play a 2nd full pass
         * = the "death sound twice" doubling.  A correctly-silenced one-shot reloads
         * len<=1; long music leads are excluded by the <3000-word SFX gate.  Logs the
         * sample + ic so the operator's "I heard a double" can be pinned.  In-game; capped. */
        if (c->len > 1 && (int)c->len == g_play_len0[ch] && c->loc == g_play_loc0[ch] &&
            c->loc >= 0x080000 && c->loc < 0x0a0000 &&   /* SFX bank only (not music / low-RAM loops) */
            g_play_len0[ch] > 16 && g_play_len0[ch] < 3000 &&
            g_os && !g_blt_busy_scope && g_log && g_double_n < 400) {
            fprintf(g_log, "DOUBLE AUD%d loc=%06x len=%d (one-shot replayed a 2nd pass) ic=%llu\n",
                    ch, c->loc, (int)c->len, (unsigned long long)g_icount);
            fflush(g_log);
            g_double_n++;
        }
        /* SCREECH on the RELOAD/SWAP path: a LOUD, SUSTAINED sample latched onto a
         * channel via underflow-reload (the engine pointing a LIVE channel at the
         * sample mid-play) does NOT go through the DMACON enable edge, so the
         * enable-only SCREECH-WATCH misses it -- which is why an identical-sounding
         * screech can be absent from the log.  Catch it here too.  Per-channel
         * throttled (~1s) so a continuous loop doesn't spam; bounded. */
        {
            uint32_t vol = g_custom[(0x0a8 + ch*0x10)>>1] & 0x7f;
            if (vol >= 48 && c->len >= 0x0800 && g_os && !g_blt_busy_scope && g_log &&
                g_screechrel_n < 300 && (g_icount - g_lastscreech_ic[ch]) > 500000ull) {
                uint32_t per = g_custom[(0x0a6 + ch*0x10)>>1];
                fprintf(g_log, "SCREECH-RELOAD AUD%d loc=%06x len=%u per=%u vol=%u ic=%llu\n",
                        ch, c->loc, (unsigned)c->len, per, vol, (unsigned long long)g_icount);
                fflush(g_log); g_screechrel_n++; g_lastscreech_ic[ch] = g_icount;
            }
        }
        /* COMBAT AUDIO TRACE (reload/swap path) -- see g_audtrace_* decl.  The screech
         * may ride in via underflow-reload (no DMACON enable edge), so trace it here too.
         * Loc-CHANGE only (no time-based re-arm, unlike the enable-edge trace): this path
         * runs on EVERY deferred-reload commit, and a channel parked on its silent loop
         * (or a looping instrument) at note-level volume commits one per word forever --
         * the old 50000-ic re-arm re-logged the SAME loc ~14 lines/sec of fprintf+fflush
         * into every shipping session (2026-07-02 audit; the operator's log carried 210
         * identical loc=00c2a4 lines).  A distinct loc still logs immediately. */
        {
            uint32_t vol = g_custom[(0x0a8 + ch*0x10)>>1] & 0x7f;
            if (g_log && g_os && !g_blt_busy_scope && g_audtrace_n < 6000
                && vol >= 24 && c->loc != g_audtrace_loc[ch]) {
                uint32_t per = g_custom[(0x0a6 + ch*0x10)>>1];
                fprintf(g_log, "AUDTRACE-RL AUD%d loc=%06x len=%u per=%u vol=%u ic=%llu\n",
                        ch, c->loc, (unsigned)c->len, per, vol, (unsigned long long)g_icount);
                fflush(g_log); g_audtrace_n++;
                g_audtrace_loc[ch] = c->loc; g_audtrace_ic[ch] = g_icount;
            }
        }
    }
    /* read the byte for the current half of the current word */
    uint32_t a = c->loc & (RAM_SIZE-1);
    /* OVER-READ DETECTOR (diagnostic): a playing voice whose read pointer lands on an
     * IFF 'FORM' header is reading sample-bank HEADER bytes as audio = the "memory-
     * error" screech the operator hears.  Log it (throttled, deduped) with the
     * offending channel / pointer / length / PC so the cause can be pinned. */
    if (c->enabled && c->wordhalf == 0 && g_log
        && g_ram[a]==0x46 && g_ram[a+1]==0x4F && g_ram[a+2]==0x52 && g_ram[a+3]==0x4D) {
        static int over_n = 0; static uint32_t over_last = 0;
        if (a != over_last && over_n < 80) {
            over_last = a; over_n++;
            fprintf(g_log, "    AUD%d OVER-READ: playing IFF 'FORM' header as audio loc=%06x len=%u per=%u pc=%06x ic=%llu\n",
                    ch, a, c->len, g_custom[(0x0a6+ch*0x10)>>1]&0xffff,
                    (unsigned)m68k_get_reg(NULL, M68K_REG_PC), (unsigned long long)g_icount);
            fflush(g_log);
        }
    }
    c->sample = (int8_t)g_ram[a + c->wordhalf];
    /* TRUMPET DIAG (--trumpetmode): the intro trumpet plays on AUD0 (lead) + AUD3 (echo ~0.37s
     * late, same sample 0x0b980c) = the "dun dun"; the real Amiga plays it once.  Output-only. */
    if (g_os && g_blt_busy_scope && g_trumpetmode == 1 && ch == 3) {
        /* STRIP AUD3's 0x0b trumpet (the late echo + the buzzy 0x0b980c/0x0b3246 idle loop) with a short
         * gain RAMP, so the bank-edge transitions don't click.  Leaves a single voice on AUD0 = the Amiga's
         * quieter sustain (removes the echo AND the 2x sustain buzz the unison mode introduced).  Runs every
         * ch3 tick so the gain can ramp back up when AUD3 leaves the 0x0b bank.  Output-only. */
        static int t3g = 256;                                       /* ch3 strip gain, 0..256 */
        int tgt = (a >= 0x0b0000u && a < 0x0c0000u) ? 0 : 256;      /* mute while reading the 0x0b trumpet bank */
        if (t3g < tgt) { t3g += 8; if (t3g > 256) t3g = 256; }
        else if (t3g > tgt) { t3g -= 8; if (t3g < 0) t3g = 0; }
        c->sample = (int8_t)(((int)c->sample * t3g) >> 8);
    } else if (g_os && g_blt_busy_scope && g_trumpetmode == 2 && ch == 3 && a >= 0x0b4000u && a < 0x0c0000u) {
        /* UNISON: mirror AUD0 only on actual NOTES (both channels >=0x0b4000) -- NOT the 0x0b3246 loop nor
         * the 0x0a trumpet (AUD0 there is <0x0b4000); mirroring those caused pitch-shift/ticks. */
        uint32_t a0 = (uint32_t)g_paula[0].loc & (RAM_SIZE-1u);
        if (a0 >= 0x0b4000u && a0 < 0x0c0000u) c->sample = g_paula[0].sample;
    }
    if (c->wordhalf == 0) {
        c->wordhalf = 1;                 /* next tick: low byte of same word */
    } else {
        c->wordhalf = 0;
        c->loc = (c->loc + 2) & (RAM_SIZE-2);
        if (c->len > 0) c->len--;
        if (c->len == 0) {
            g_first_pass[ch] = 0;   /* underflowed -> no longer on the first pass (CUT-OFF watch) */
            /* Buffer exhausted: raise the AUDx IRQ NOW (so the level-4 sound-server
             * ISR runs), but DEFER the AUDxLC/LEN reload to the next tick (set
             * reload_pending) -- see the note above.  The held DAC sample carries
             * over the one bridging tick (faithful: Paula holds its last value). */
            c->reload_pending = 1;
            /* Classify the silent-loop idiom for the deferred reload (see the
             * reload_isr_wait note in PaulaCh + the handling at the top of this
             * function).  PRE-ARMED servers (low-RAM, AUD masked in INTENA) write
             * AUDxLEN<=1 directly right after ENABLE -- the regs already hold the
             * silent loop, so the existing 1-tick defer commits silence: keep it
             * (reload_isr_wait stays 0).  ISR-DRIVEN servers (Mog: enables the AUD
             * interrupt in INTENA; the L4 AUDx ISR writes the silent loop) only
             * have it installed once the ISR runs -- so hold the reload until the
             * ISR ACKs.  Discriminator = AUD IRQ enabled in INTENA at underflow. */
            {
                uint16_t ena = g_custom[0x09a>>1];
                int isr_driven = (ena & 0x4000) && (ena & (uint16_t)(0x0080 << ch));
                c->reload_isr_wait  = isr_driven;
                c->reload_wait_ticks = 0;
                g_len_armed[ch] = 0;   /* require AUDxLEN to be (re)written before the deferred reload latches */
            }
            g_custom[0x09c>>1] |= (uint16_t)(0x0080 << ch);   /* INTREQ AUDx */
            update_ipl();
            g_aud_irq[ch]++;
            /* Throttle the IRQ log: a one-shot SFX that finished and is now
             * looping on its 1-word silent buffer (LEN==1) raises an AUDx IRQ
             * every word forever (HW-faithful), which would flood --audlog and
             * bury the meaningful ENABLE/register events at later scene beats.
             * Log only the first such silent-loop reload per (ch,loc).  We read the
             * loop target from the registers (what the deferred reload will use). */
            if (g_audlog && g_log && g_audlog_n < 40000) {
                static uint32_t last_silent_loc[4] = {0,0,0,0};
                uint32_t b = 0x0a0 + ch*0x10;
                uint32_t rl = (((uint32_t)g_custom[b>>1]<<16)|g_custom[(b+2)>>1])&(RAM_SIZE-2);
                uint16_t rn = g_custom[(b+4)>>1];
                int silent_loop = (rn <= 1);
                if (!silent_loop || last_silent_loc[ch] != rl) {
                    if (silent_loop) last_silent_loc[ch] = rl;
                    else             last_silent_loc[ch] = 0xffffffff;
                    fprintf(g_log, "    AUD%d IRQ (reload loc=%06x len=%u) ic=%llu%s\n",
                            ch, rl, rn, (unsigned long long)g_icount,
                            silent_loop ? " [one-shot done -> silent loop]" : "");
                    g_audlog_n++;
                }
            }
        }
    }
}

/* (re)evaluate channel enables from DMACON; latch newly-started channels */
static void paula_update_dma(void) {
    uint16_t dmacon = g_custom[0x096>>1];
    int master = (dmacon & 0x0200) != 0;        /* DMAEN bit9 */
    for (int ch = 0; ch < 4; ch++) {
        int en = master && (dmacon & (1u << ch));   /* AUDxEN bits 0..3 */
        if (en && !g_paula[ch].enabled) {
            /* DMA (re)start: latch the new ptr/len NOW and prime the held DAC
             * value from the NEW buffer's first byte.  HW fetches the first
             * sample word right after a restart; if we instead deferred the
             * latch to the next host-tick the channel would keep emitting the
             * PREVIOUS buffer's last sample for the few host-samples it takes
             * the resampling accumulator to cross — at the new (often much
             * louder) volume that produces an audible CRACKLE/POP when a music
             * channel is re-triggered for a one-shot SFX (forest scene). */
            paula_latch(ch);                   /* sets loc/len from regs, first=0 */
            g_paula[ch].reload_pending = 0;    /* fresh start: no deferred reload */
            g_paula[ch].reload_isr_wait = 0;   /* and no pending ISR-wait hold     */
            g_paula[ch].reload_wait_ticks = 0;
            g_paula[ch].sample = (int8_t)g_ram[g_paula[ch].loc & (RAM_SIZE-1)];
            g_paula[ch].wordhalf = 1;          /* next byte is the low byte */
            g_paula[ch].acc = 0;
            g_paula[ch].attack = 0;            /* arm the onset de-click ramp (see g_attack_ramp) */
            paula_recalc_step(ch);
            g_play_len0[ch] = g_paula[ch].len; /* remember the sample length for the CUT-OFF watch */
            g_play_loc0[ch] = g_paula[ch].loc; /* and the start, for the REPLAY/DOUBLE watch */
            g_first_pass[ch] = 1;              /* fresh sample: on its first pass (not yet underflowed) */
            if (g_audlog && g_log && g_audlog_n < 40000) {
                uint32_t b = 0x0a0 + ch*0x10;
                fprintf(g_log, "    AUD%d ENABLE loc=%06x len=%u per=%u vol=%u ic=%llu\n",
                        ch,
                        (((uint32_t)g_custom[b>>1]<<16)|g_custom[(b+2)>>1])&(RAM_SIZE-2),
                        g_custom[(b+4)>>1], g_custom[(b+6)>>1], g_custom[(b+8)>>1],
                        (unsigned long long)g_icount);
                g_audlog_n++;
            }
            /* ALWAYS-ON SCREECH WATCH (no F9 needed): the operator's "screech" is a
             * LOUD, SUSTAINED (full-length, non-looping) sample.  In-game only
             * (g_os && attract scope cleared) so intro music doesn't flood it; only
             * vol>=48 AND len>=0x1000 words (~1s) so normal short SFX / short music
             * loops are excluded.  Logs the sample + the PC that triggered the enable
             * (tells music-sequencer vs SFX path) so the next occurrence is captured. */
            if (g_log && g_os && !g_blt_busy_scope && g_screech_n < 400) {
                uint32_t b   = 0x0a0 + ch*0x10;
                uint32_t loc = (((uint32_t)g_custom[b>>1]<<16)|g_custom[(b+2)>>1])&(RAM_SIZE-2);
                uint32_t len = g_custom[(b+4)>>1], per = g_custom[(b+6)>>1];
                uint32_t vol = g_custom[(b+8)>>1] & 0x7f;
                if (vol >= 48 && len >= 0x0800) {   /* widened 0x1000->0x800: catch shorter/quieter
                                                     * screech variants too (normal SFX are <2048 words) */
                    fprintf(g_log, "SCREECH-WATCH AUD%d loc=%06x len=%u per=%u vol=%u pc=%06x ic=%llu\n",
                            ch, loc, len, per, vol,
                            (unsigned)m68k_get_reg(NULL, M68K_REG_PPC),
                            (unsigned long long)g_icount);
                    /* FORENSIC (once per distinct sample): the call chain (stack return
                     * addresses) + selection registers, so a RARE ear-hurting screech can
                     * be told apart from a normal note -- the engine plays SFX *and* music
                     * through the same voice player, so we must see WHO asked for it and
                     * with what index/length (a combat hit grabbing the wrong long sample
                     * would show a different caller / anomalous selection regs). */
                    {
                        static uint32_t seen[16]; static int sn = 0; int known = 0;
                        for (int k = 0; k < 16; k++) if (seen[k] == loc) { known = 1; break; }
                        if (!known) {
                            seen[sn & 15] = loc; sn++;
                            uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
                            fprintf(g_log, "  SCREECH-CTX loc=%06x stack=%06x,%06x,%06x,%06x,%06x,%06x,%06x,%06x "
                                    "D0=%x D1=%x D2=%x D3=%x A2=%06x A3=%06x A4=%06x\n", loc,
                                    (unsigned)r32(sp),(unsigned)r32(sp+4),(unsigned)r32(sp+8),(unsigned)r32(sp+12),
                                    (unsigned)r32(sp+16),(unsigned)r32(sp+20),(unsigned)r32(sp+24),(unsigned)r32(sp+28),
                                    (unsigned)m68k_get_reg(NULL,M68K_REG_D0),(unsigned)m68k_get_reg(NULL,M68K_REG_D1),
                                    (unsigned)m68k_get_reg(NULL,M68K_REG_D2),(unsigned)m68k_get_reg(NULL,M68K_REG_D3),
                                    (unsigned)m68k_get_reg(NULL,M68K_REG_A2),(unsigned)m68k_get_reg(NULL,M68K_REG_A3),
                                    (unsigned)m68k_get_reg(NULL,M68K_REG_A4));
                        }
                    }
                    fflush(g_log);
                    g_screech_n++;
                }
            }
            /* COMBAT AUDIO TRACE (enable edge) -- see g_audtrace_* decl. Catches the
             * short/pitched combat screech that SCREECH-WATCH's len>=0x800 gate misses. */
            if (g_log && g_os && !g_blt_busy_scope && g_audtrace_n < 6000) {
                uint32_t b   = 0x0a0 + ch*0x10;
                uint32_t loc = (((uint32_t)g_custom[b>>1]<<16)|g_custom[(b+2)>>1])&(RAM_SIZE-2);
                uint32_t len = g_custom[(b+4)>>1], per = g_custom[(b+6)>>1];
                uint32_t vol = g_custom[(b+8)>>1] & 0x7f;
                if (vol >= 24 && (loc != g_audtrace_loc[ch] || (g_icount - g_audtrace_ic[ch]) > 50000ull)) {
                    fprintf(g_log, "AUDTRACE-EN AUD%d loc=%06x len=%u per=%u vol=%u pc=%06x ic=%llu\n",
                            ch, loc, len, per, vol,
                            (unsigned)m68k_get_reg(NULL, M68K_REG_PPC),
                            (unsigned long long)g_icount);
                    fflush(g_log); g_audtrace_n++;
                    g_audtrace_loc[ch] = loc; g_audtrace_ic[ch] = g_icount;
                }
            }
            /* ALWAYS-ON RETRIGGER WATCH: the same SFX-bank sample ENABLED again within
             * ~0.6s (on this or another voice) = the game re-fired it = a perceived
             * "double" that is NOT the reload-replay class (which the guard handles).
             * Distinguishes a game re-fire from a reload double.  In-game; SFX bank only. */
            if (g_log && g_os && !g_blt_busy_scope) {
                uint32_t loc = g_paula[ch].loc;
                if (loc >= 0x080000 && loc < 0x0a0000) {
                    for (int k = 0; k < 8; k++) {
                        if (g_last_sfx_loc[k] == loc && (g_icount - g_last_sfx_ic[k]) < 300000ull) {
                            if (g_retrig_n < 200) {
                                fprintf(g_log, "RETRIGGER AUD%d loc=%06x gap_ic=%llu ic=%llu\n",
                                        ch, loc, (unsigned long long)(g_icount - g_last_sfx_ic[k]),
                                        (unsigned long long)g_icount);
                                fflush(g_log); g_retrig_n++;
                            }
                            break;
                        }
                    }
                    static int ring = 0;
                    g_last_sfx_loc[ring] = loc; g_last_sfx_ic[ring] = g_icount; ring = (ring+1) & 7;
                }
            }
        }
        else if (!en && g_paula[ch].enabled) {
            /* ALWAYS-ON CUT-OFF WATCH: a one-shot SFX disabled while still on its
             * FIRST pass (g_first_pass -- it never underflowed) with a big chunk of
             * the sample unplayed = the sound was TRUNCATED ("mreow" -> "m..").
             * g_first_pass excludes normal completions (which end via underflow ->
             * silent loop) and looping music (which has underflowed at least once).
             * In-game only; logs sample/length/how-much-was-left/PC; bounded. */
            if (g_first_pass[ch] && g_play_len0[ch] > 16 &&
                (int)g_paula[ch].len > g_play_len0[ch]/4 &&
                g_os && !g_blt_busy_scope && g_log && g_cutoff_n < 400) {
                int total = g_play_len0[ch], rem = (int)g_paula[ch].len;
                fprintf(g_log, "CUT-OFF AUD%d loc=%06x total=%d remaining=%d (%d%% unplayed) pc=%06x ic=%llu\n",
                        ch, g_paula[ch].loc, total, rem, 100*rem/total,
                        (unsigned)m68k_get_reg(NULL, M68K_REG_PPC),
                        (unsigned long long)g_icount);
                fflush(g_log);
                g_cutoff_n++;
            }
            g_first_pass[ch] = 0;
        }
        g_paula[ch].enabled = en;
    }
}

/* Rebuild the reload-protocol SIDECAR statics after a savestate load.  These
 * (g_len_armed / g_play_loc0 / g_play_len0 / g_first_pass) live outside the
 * save blob (a new PaulaCh field would break save-version compat -- see the
 * g_len_armed decl), yet PaulaCh.reload_pending/reload_isr_wait ARE restored,
 * so a load mid-reload used to run the protocol on whatever the loading
 * process's statics happened to hold: a stale g_len_armed=1 re-opened the
 * pre-fix mid-install latch race (the combat screech class), and stale
 * loc0/len0 either disarmed the SFX double-guard (cold --loadstate) or falsely
 * armed it (same-session F9).  Conservative reconstruction from the restored
 * registers:
 *   - g_len_armed=0: an ISR-wait reload must see a FRESH AUDxLEN write before
 *     latching; if the save caught a silent-loop install mid-flight the
 *     resumed ISR provides it, else the bounded 64-tick held-DAC commits the
 *     (coherently snapshotted) registers.  Never the mixed old-LEN latch.
 *   - loc0/len0 = the restored AUDxLC/LEN: if the registers still point at
 *     the just-played one-shot (the replay hazard the guard exists for) the
 *     guard arms exactly as it should; a silent loop already installed
 *     differs (len<=1) and leaves the guard quiet.
 *   - g_first_pass=0: diag-only (CUT-OFF watch) -- no bogus lines post-load. */
static void paula_sidecar_reset(void) {
    for (int ch = 0; ch < 4; ch++) {
        uint32_t b = 0x0a0 + ch*0x10;
        g_len_armed[ch]  = 0;
        g_play_loc0[ch]  = (((uint32_t)g_custom[b>>1]<<16)|g_custom[(b+2)>>1]) & (RAM_SIZE-2);
        g_play_len0[ch]  = (int)g_custom[(b+4)>>1];
        g_first_pass[ch] = 0;
    }
}

/* Generate `nframes` stereo output samples (interleaved L,R) into out[] from the
 * current Paula state, advancing each channel by Paula sample-ticks.  Each host
 * sample we add `step` (16.16) to each channel's accumulator and tick it once
 * per whole channel-sample crossed; the held DAC value * volume is summed into
 * the L/R buses per the ch0,3=L / ch1,2=R map. */
static void paula_generate(int16_t *out, int nframes) {
    for (int n = 0; n < nframes; n++) {
        int32_t left = 0, right = 0;
        for (int ch = 0; ch < 4; ch++) {
            PaulaCh *c = &g_paula[ch];
            if (!c->enabled) continue;
            if (g_solo >= 0 && ch != g_solo) continue;   /* --solo: isolate one channel */
            if (g_mute_mask & (1<<ch)) continue;          /* --mute: drop one channel, keep the rest in context */
            c->acc += c->step;
            while (c->acc >= 0x10000) { c->acc -= 0x10000; paula_tick_one(ch); }
            int vol = g_custom[(0x0a8 + ch*0x10)>>1] & 0x7f;
            if (vol > 64) vol = 64;
            if (g_audstat && g_log) {                    /* --audstat: trace per-channel vol/per changes */
                int per = g_custom[(0x0a6 + ch*0x10)>>1] & 0xffff;
                if (vol != g_audstat_pv[ch] || per != g_audstat_pp[ch]) {
                    fprintf(g_log, "AUDSTAT fr=%d AUD%d vol=%d per=%d loc=%06x len=%u ic=%llu\n",
                            g_cur_frame, ch, vol, per, (unsigned)c->loc, (unsigned)c->len,
                            (unsigned long long)g_icount);
                    g_audstat_pv[ch] = vol; g_audstat_pp[ch] = per;
                }
            }
            int32_t samp = c->sample;                    /* zero-order hold (held DAC value) */
            if (g_interp) {                              /* linear-interp to next sample: smooth the ZOH staircase (de-tick, no tone loss) */
                int8_t s_next = (int8_t)g_ram[(c->loc + c->wordhalf) & (RAM_SIZE-1)];
                samp = ((int32_t)c->sample * (int32_t)(0x10000u - c->acc) + (int32_t)s_next * (int32_t)c->acc) >> 16;
            }
            /* --- BEEP-OUT detector (see g_beepout_* decl): zero-crossing pitch estimate --- */
            if (g_log && g_os && !g_blt_busy_scope && vol >= 24) {
                if (samp > 4)      { if (g_bo_prevsign[ch] < 0) g_bo_cross[ch]++; g_bo_prevsign[ch] = 1; }
                else if (samp < -4){ if (g_bo_prevsign[ch] > 0) g_bo_cross[ch]++; g_bo_prevsign[ch] = -1; }
                if (++g_bo_win[ch] >= 220) {            /* ~5ms @44100 */
                    int est = g_bo_cross[ch] * 100;     /* freq ~= cross*44100/(2*220) ~= cross*100 */
                    if (est >= 3000) {                  /* sustained >3kHz on a loud channel = beep */
                        if (++g_bo_sustain[ch] >= 2 && g_beepout_n < 200
                            && (g_icount - g_bo_lastlog_ic[ch]) > 200000ull) {
                            uint32_t per = g_custom[(0x0a6 + ch*0x10)>>1];
                            fprintf(g_log, "BEEP-OUT AUD%d ~%dHz loc=%06x len=%u per=%u vol=%d ic=%llu\n",
                                    ch, est, (unsigned)g_paula[ch].loc, (unsigned)g_paula[ch].len,
                                    per, vol, (unsigned long long)g_icount);
                            fflush(g_log); g_beepout_n++; g_bo_lastlog_ic[ch] = g_icount;
                        }
                    } else {
                        g_bo_sustain[ch] = 0;
                    }
                    g_bo_cross[ch] = 0; g_bo_win[ch] = 0;
                }
            }
            int32_t s = samp * vol;
            if (g_attack_ramp && c->attack < g_attack_ramp) {  /* de-click: fade only the note ONSET, not the tone */
                s = (int32_t)((int64_t)s * c->attack / g_attack_ramp);
                c->attack++;
            }
            /* Per-channel de-click LP on the AUD2 bass "boom" (the right-ear "tick").
             * AUD2 is ~98.5% sub-bass (<250 Hz); its ONLY >1.5 kHz content is the sharp
             * note-attack transient.  A low one-pole on THIS channel alone removes the
             * tick with no audible dulling (nothing musical lives up there) and leaves
             * the bright melody channels untouched -- unlike a global low-pass, which
             * dulls everything (operator rejected that).  Run the filter on every AUD2
             * sample so its state stays continuous; only SUBSTITUTE the filtered value
             * during the attract intro on a bass note (high period).  Output-only ->
             * determinism-neutral. */
            /* Per-channel ATTACK LIMITER on AUD1 = the right-ear "tick" (ear-confirmed).
             * AUD1 plays a faithful in-sample note attack that swells ~7x every note; it
             * does NOT re-trigger (so attack-ramp can't help) and the swell is <1kHz (so a
             * low-pass only dulls).  A peak-follower + downward compressor pulls ONLY the
             * loud attack swells toward AUD1's sustain, softening the rhythmic tick with NO
             * tone/frequency change.  Gain comes from the SMOOTHED envelope (fast attack /
             * slow release) so it never zipper-distorts.  Attract intro only; output-only
             * -> determinism-neutral. */
            if (ch == 1 && g_aud1lim_thr && g_os && g_blt_busy_scope) {
                double ax = (s < 0) ? -(double)s : (double)s;
                double coef = (ax > g_aud1lim_env) ? 0.06 : 0.0004;  /* attack ~0.4ms / release ~57ms */
                g_aud1lim_env += (ax - g_aud1lim_env) * coef;
                if (g_aud1lim_env > (double)g_aud1lim_thr) {
                    double gain = pow(g_aud1lim_env / (double)g_aud1lim_thr, 1.0/(double)g_aud1lim_ratio - 1.0);
                    s = (int32_t)((double)s * gain);
                }
            }
            if (ch == 2 && g_boomlp_a) {
                g_boomlp_y1 += (int32_t)(((int64_t)(s          - g_boomlp_y1) * g_boomlp_a) >> 16);
                g_boomlp_y2 += (int32_t)(((int64_t)(g_boomlp_y1 - g_boomlp_y2) * g_boomlp_a) >> 16);
                int per2 = g_custom[(0x0a6 + 2*0x10)>>1] & 0xffff;
                if (g_os && g_blt_busy_scope && per2 >= 280) s = g_boomlp_y2;
            }
            if (ch == 0 || ch == 3) left += s; else right += s;
        }
        /* Amiga Paula is HARD-panned (ch0+3 hard-left, ch1+2 hard-right), which
         * makes melodies ping-pong between the ears. Apply a stereo crossfeed
         * (blend g_xfeed% of each side into the other) for a natural image.
         * g_xfeed=0 → faithful hard-pan (--hardpan); 50 → mono. Default ~35. */
        int32_t L = (left*(100 - g_xfeed) + right*g_xfeed) / 100;
        int32_t R = (right*(100 - g_xfeed) + left*g_xfeed) / 100;
        /* 2 channels/side * 8192 peak = +-16384; scale up ~*2 for headroom-safe
         * 16-bit output, then clamp.  (*2, not <<1: left-shifting a negative
         * signed value is undefined in ISO C -- C99 6.5.7p4.) */
        L *= 2; R *= 2;
        if (L >  32767) L =  32767; else if (L < -32768) L = -32768;
        if (R >  32767) R =  32767; else if (R < -32768) R = -32768;
        out[2*n]   = (int16_t)L;
        out[2*n+1] = (int16_t)R;
    }
}

/* ---- host audio output: SDL queue (live) and/or WAV capture (validation) ---- */
static SDL_AudioDeviceID g_audio_dev = 0;
/* Intro A/V-sync lever (signed offset, in video frames; --avdelay N; attract only).
 * The attract intro's lightning thunder doesn't land on its flash.  Rather than
 * slowing the whole intro (the old blit-busy / DMA-steal throttle, now inert at 0),
 * we shift audio vs. video by g_av_delay_frames:
 *
 *   N > 0  -> DELAY THE AUDIO N frames (audio plays late).  Fixes thunder that
 *            fires AHEAD of the flash.  Done by holding the SDL audio queue ~N
 *            frames deep (prime N frames of silence, keep the backlog that deep).
 *
 *   N < 0  -> DELAY THE VIDEO |N| frames (audio leads the picture).  Fixes thunder
 *            that lands AFTER the flash -- we can't play the sound earlier than it
 *            is generated, so instead we hold the *display* back |N| frames via a
 *            ring of captured frames, letting the flash move later onto the sound.
 *
 * At Mog launch both policies end (audio backlog drained to ~2 frames; video snaps
 * to live) so gameplay stays responsive.  -125 = video held ~2.5 s (sound leads). */
static int g_av_delay_frames = -135;      /* signed attract A/V offset in frames (--avdelay); <0 delays video.
                                           * -135 (operator request 2026-06-24, from -150): 15 frames / 0.3s less
                                           * video delay, i.e. audio leads the picture by slightly less. */
static int g_av_attract_audio = 0;        /* 1 while the attract audio-delay (deep-backlog) policy is active */
static int g_av_attract_video = 0;        /* 1 while the attract video-delay (frame-ring) policy is active */
static int g_av_drain = 0;                /* 1 = Mog launched: drain the buffered intro tail, then go live */
/* The -2.5s video hold lines up the intro lightning (operator-confirmed), but its
 * ~2.5s tail used to linger into the intro->gameplay transition (the Mog-launch
 * drain fired too late to hide the load black).  g_av_ramp is armed at the LEGEND
 * scene (PC 0x210f6) -- the final pre-game screen, AFTER all thunder/lightning, so
 * the confirmed sync is untouched -- and tells run_sdl to walk the video delay back
 * down to 0 across the legend.  By Mog launch the picture is caught up to live, so
 * the delay contributes no black at the hand-off (the floppy-wait fix removes the
 * load dead time; this removes the delay tail). */
static int g_av_ramp = 0;                 /* 1 = ramp the attract video delay down to 0 (set at legend scene) */
static FILE  *g_wav = NULL;            /* --wav: capture file */
static uint32_t g_wav_bytes = 0;       /* data-chunk byte count (patched on close) */
/* running stats over all generated samples (for validation reporting) */
static int32_t  g_a_min = 0, g_a_max = 0;
static uint64_t g_a_nonzero = 0, g_a_total = 0;
static double   g_a_sumsq = 0.0;

static void wav_write_header(FILE *f, uint32_t data_bytes) {
    uint32_t srate = HOST_RATE, brate = HOST_RATE*2*2;
    uint16_t chans = 2, bits = 16, balign = 4;
    uint32_t riff = 36 + data_bytes, fmtlen = 16; uint16_t fmt = 1;
    fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); fwrite(&fmtlen,4,1,f); fwrite(&fmt,2,1,f); fwrite(&chans,2,1,f);
    fwrite(&srate,4,1,f); fwrite(&brate,4,1,f); fwrite(&balign,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&data_bytes,4,1,f);
}

/* Base window title (set in run_sdl); used to restore the title after a [REC] tag. */
static char g_wintitle[160] = "Moonstone (native)";

/* F9 in the live window toggles audio capture: writes capture-N.wav next to the
 * exe so the operator can record an exact glitch and hand back the real waveform
 * (closes the "I can't hear it" gap for audio debugging).  Reuses the g_wav sink
 * that audio_flush() already writes each frame. */
static void toggle_record(SDL_Window *win) {
    static int n = 0;
    static int saved_audlog = 0;
    if (g_wav) {                                   /* stop + finalize */
        uint32_t bytes = g_wav_bytes;
        fseek(g_wav, 0, SEEK_SET); wav_write_header(g_wav, bytes);
        fclose(g_wav); g_wav = NULL;
        g_audlog = saved_audlog;                   /* restore Paula reg-logging state */
        if (win) SDL_SetWindowTitle(win, g_wintitle);
        if (g_log) { fprintf(g_log, "=== audio capture %d STOP ic=%llu (capture-%d.wav, %u bytes) ===\n",
                             n, (unsigned long long)g_icount, n, bytes); fflush(g_log); }
    } else {                                        /* start */
        char p[1280];
        snprintf(p, sizeof(p), "%s%scapture-%d.wav",
                 g_exedir[0] ? g_exedir : ".", g_exedir[0] ? "/" : "", ++n);
        g_wav = fopen(p, "wb");
        if (g_wav) {
            g_wav_bytes = 0; wav_write_header(g_wav, 0); g_audio_on = 1;
            /* Also log Paula register/DMA/IRQ activity for this capture window so a
             * captured glitch can be diagnosed against the actual channel triggers.
             * Goes to the main log (moonstone.log), delimited by these ic= markers. */
            saved_audlog = g_audlog; g_audlog = 1; g_audlog_n = 0;
            char t[200]; snprintf(t, sizeof(t), "%s  [REC %d]", g_wintitle, n);
            if (win) SDL_SetWindowTitle(win, t);
            if (g_log) { fprintf(g_log, "=== audio capture %d START ic=%llu -> %s (reg-log on) ===\n",
                                 n, (unsigned long long)g_icount, p); fflush(g_log); }
        } else { n--; if (g_log) { fprintf(g_log, "audio capture FAILED to open %s\n", p); fflush(g_log); } }
    }
}

/* Per-frame sample buffer + how many of this frame's 882 samples are already
 * generated.  Audio is generated INCREMENTALLY, interleaved with the CPU bursts
 * (audio_advance), then pushed to the host sink once at end-of-frame
 * (audio_flush).  This is what makes a Paula AUDx level-4 interrupt FAITHFUL:
 * when a one-shot SFX hits buffer-end mid-frame it raises its AUDx INTREQ, and
 * because the next m68k_execute burst runs the game's L4 sound-server ISR
 * (e.g. gameplay server @0x043224 -> repoint AUDxLC/LEN to a 1-word silent
 * loop) BEFORE we generate more than a sub-burst of audio, the channel is
 * silenced before it can replay the sample a second time.  With the old
 * once-per-frame batched generation the ISR was serviced a full frame late, so
 * the channel auto-looped its full sample and the SFX was heard TWICE (enemy
 * grunt / sword strike doubling in combat).  Real Paula raises the IRQ the
 * instant the buffer drains and the 68k services it in microseconds; advancing
 * ~1 sample per 160-cycle CPU burst models that quantum far finer than a frame.
 */
static int16_t g_frame_buf[SMP_PER_FRAME*2];
static int     g_smp_done = 0;     /* samples of the current frame already generated */

/* Re-prime the SDL queue with N frames of silence.  The boot path establishes the
 * anti-underrun cushion ONCE (run_sdl's prime); after that, generation == playback
 * (1 frame per video frame), so a cushion can never rebuild by itself -- the
 * audio_flush queue cap only LIMITS pushes, it never adds any.  Every
 * SDL_ClearQueuedAudio site therefore left the session permanently at <1 frame of
 * queue with periodic hard-zero underruns (root-caused 2026-07-02: the operator's
 * chronic AQ-UNDERRUN log was an intro-skip session; repro'd exactly with
 * --skipat).  Silence, not duplicated audio: a one-time ~N*20ms latency step, no
 * audible splice, same trade the boot prime already makes. */
static void audio_reprime(int frames) {
    if (!g_audio_dev || frames <= 0) return;
    size_t n = (size_t)frames * SMP_PER_FRAME * 2;
    int16_t *sil = (int16_t *)calloc(n, sizeof(int16_t));
    if (!sil) return;
    SDL_QueueAudio(g_audio_dev, sil, (Uint32)(n * sizeof(int16_t)));
    free(sil);
    if (g_log) { fprintf(g_log, "AQ-REPRIME frames=%d q=%u bytes\n", frames,
                         (unsigned)SDL_GetQueuedAudioSize(g_audio_dev)); fflush(g_log); }
}

/* One-pole low-pass on the final mix, modelling the Amiga's FIXED analog output
 * RC filter (~5 kHz, always on -- distinct from the switchable "LED" filter) that
 * our raw-sample Paula model omits.  Without it, hard VOL/DMA-restart steps at note
 * attacks stay as razor edges that read as faint "ticks"; the real hardware rounds
 * them off.  Coeff = alpha<<16 (one-pole y += (x-y)*alpha); 0 = off.  Default 27265 ~= a 5 kHz one-pole
 * @ 44100 (the A500 fixed filter); halves the attack-edge jump (6016->3167).  --lpf <Hz> overrides
 * (0 = off / brightest; 3300 = the stronger switchable "LED" filter). */
static int     g_lpf_a = 27265;

/* A500 "LED" output filter, modelled as a proper Butterworth 2-pole low-pass in
 * SERIES with the fixed RC pole above.  A note ONSET is a near-instant step; even
 * after the fixed pole it keeps a razor edge that reads as a faint "tick" on each
 * note (most audible on the right-channel "boom" now the AUDxVOL byte-write fix plays
 * it at its correct, louder level).  My first attempt used two cascaded one-poles at
 * 3.3 kHz -- that DROOPS the passband and dulled the whole mix (operator 2026-06-24:
 * "makes the audio so dull").  A Butterworth has a FLAT passband (stays bright) with
 * a -12 dB/oct top-end that rounds the attack transient: 1p5k + Butterworth 5.5k
 * drops the attack edge ~43% (310->177) at negligible brightness cost.  --ledfilter
 * <Hz> sets the cutoff (0 = off / brightest).  Output-only (CPU-trace neutral). */
static double  g_led_fc = 5500.0;   /* LED Butterworth cutoff Hz (0 = off).  ON by default: the
                                     * interpolation route (below) backfired with loop-boundary
                                     * glitches, so this Butterworth is the working de-tick -- flat
                                     * passband stays bright, cuts the attack tick ~43% (310->177)
                                     * at a small brightness cost.  --ledfilter <Hz> to tune/off. */
static int     g_led_on = 0;        /* coeffs valid + filter active (set by led_set_cutoff) */
static double  g_led_b0,g_led_b1,g_led_b2,g_led_a1,g_led_a2;  /* RBJ biquad coeffs (normalised) */
static void led_set_cutoff(double fc) {
    g_led_fc = fc;
    if (fc <= 0.0) { g_led_on = 0; return; }
    double w0 = 6.283185307*fc/44100.0, c = cos(w0), s = sin(w0), al = s/(2.0*0.70710678);
    double a0 = 1.0 + al;
    g_led_b0 = (1.0-c)/2.0/a0; g_led_b1 = (1.0-c)/a0; g_led_b2 = g_led_b0;
    g_led_a1 = -2.0*c/a0;      g_led_a2 = (1.0-al)/a0;
    g_led_on = 1;
}

/* Generate audio up to sample index `upto` (0..SMP_PER_FRAME) of the current
 * frame.  Called after each CPU burst with upto scaled to the beam position so
 * a mid-frame AUDx IRQ is serviced by the next burst before the doubled pass
 * accumulates.  No-op (and thus CPU-trace-neutral) when audio is off. */
static void audio_advance(int upto) {
    if (!g_audio_on) return;
    if (upto > SMP_PER_FRAME) upto = SMP_PER_FRAME;
    if (upto <= g_smp_done) return;
    int n = upto - g_smp_done;
    paula_generate(&g_frame_buf[g_smp_done*2], n);
    /* Amiga output filters -- two INDEPENDENT cascaded stages: RC 1-pole (always-on analog)
     * then LED Butterworth 2-pole.  Each toggles on its own (--lpf 0 / --ledfilter 0);
     * previously the LED was nested inside the RC's gate, so --lpf 0 killed both. */
    { static int led_init = 0; if (!led_init) { led_set_cutoff(g_led_fc); led_init = 1; } }
    if (g_lpf_a || g_led_on) {
        static int32_t yl = 0, yr = 0;   /* RC 1-pole state (persists across frames) */
        static double lx1=0,lx2=0,ly1=0,ly2=0, rx1=0,rx2=0,ry1=0,ry2=0;  /* LED Butterworth biquad state */
        for (int i = g_smp_done; i < upto; i++) {
            int32_t ol = g_frame_buf[i*2], orr = g_frame_buf[i*2+1];
            if (g_lpf_a) {               /* stage 1: A500 fixed output RC (de-clicks attack edges) */
                int32_t dl = (int32_t)(((int64_t)(ol  - yl) * g_lpf_a) >> 16);
                int32_t dr = (int32_t)(((int64_t)(orr - yr) * g_lpf_a) >> 16);
                /* the arithmetic >>16 floors toward -inf, so a state 1-2 LSB BELOW the
                 * input computes a zero step and sticks there forever (e.g. yl=-1 on
                 * silence: (1*27265)>>16 == 0) -- a permanent -1 LSB DC tail after any
                 * negative-side decay that also poisoned the g_a_nonzero stat and WAV
                 * A/B silence tails.  Force a 1-LSB step whenever the state hasn't
                 * converged: symmetric terminal behavior, exact convergence to input. */
                if (dl == 0 && ol != yl) dl = (ol > yl) ? 1 : -1;
                if (dr == 0 && orr != yr) dr = (orr > yr) ? 1 : -1;
                yl += dl; yr += dr;
                ol = yl; orr = yr;
            }
            if (g_led_on) {              /* stage 2: A500 "LED" Butterworth 2-pole: rounds attack edges; flat passband keeps it bright */
                double nl = g_led_b0*ol  + g_led_b1*lx1 + g_led_b2*lx2 - g_led_a1*ly1 - g_led_a2*ly2;
                lx2=lx1; lx1=ol;  ly2=ly1; ly1=nl;
                double nr = g_led_b0*orr + g_led_b1*rx1 + g_led_b2*rx2 - g_led_a1*ry1 - g_led_a2*ry2;
                rx2=rx1; rx1=orr; ry2=ry1; ry1=nr;
                ol = (int32_t)nl; orr = (int32_t)nr;
            }
            /* SATURATE to int16 -- the Butterworth can ring ~4% past a full-scale
             * clipped edge; a raw cast would WRAP that to a sign-flipped spike (click).
             * Clamping only ever replaces a wrap-spike with a clean flat-top: no-loss. */
            if (ol  >  32767) ol  =  32767; else if (ol  < -32768) ol  = -32768;
            if (orr >  32767) orr =  32767; else if (orr < -32768) orr = -32768;
            g_frame_buf[i*2]   = (int16_t)ol;
            g_frame_buf[i*2+1] = (int16_t)orr;
        }
    }
    g_smp_done = upto;
}

/* Finish any remaining samples of the frame (guarantees exactly 882/frame, no
 * rate drift even when the cycle math rounds short), then run stats + push the
 * whole frame to the open host sink(s).  Called once per frame after the CPU. */
static void audio_flush(void) {
    if (!g_audio_on) { g_smp_done = 0; return; }
    audio_advance(SMP_PER_FRAME);    /* top up to a full frame */
    int16_t *buf = g_frame_buf;
    /* accumulate stats */
    for (int i = 0; i < SMP_PER_FRAME*2; i++) {
        int32_t s = buf[i];
        if (g_a_total == 0) { g_a_min = g_a_max = s; }
        if (s < g_a_min) g_a_min = s; if (s > g_a_max) g_a_max = s;
        if (s != 0) g_a_nonzero++;
        g_a_sumsq += (double)s * (double)s;
        g_a_total++;
    }
    if (g_wav) { fwrite(buf, sizeof(int16_t), SMP_PER_FRAME*2, g_wav); g_wav_bytes += SMP_PER_FRAME*2*sizeof(int16_t); }
    if (g_audio_dev && !g_audio_mute) {
        const Uint32 frame_bytes = (Uint32)(SMP_PER_FRAME*2*sizeof(int16_t));
        /* Hold the device PAUSED (playing only the primed silence cushion) until the
         * first AUDIBLE frame -- i.e. until the disk-load finishes and the attract
         * audio actually starts.  Otherwise the device drains the prime to empty during
         * the multi-second load, then runs chronically starved (queue ~0.5 frame) for
         * the whole intro -- the "tick" was constant underrun (confirmed live: 3161
         * AQ-UNDERRUN, 0 drops, q hitting 0.00 from frame 1).  While paused we do NOT
         * queue (the queue would otherwise fill with seconds of load-silence); the init
         * prime is the cushion.  On the first audible frame we unpause -- the queue then
         * holds at the prime depth because generation (1 frame/video-frame) == playback,
         * so jitter no longer drains it to empty.  Live-only; headless/WAV (no device)
         * is untouched, so determinism is unaffected. */
        if (g_audio_paused) {
            /* AQ-PAUSED diag (cushion-loss hunt 2026-07-02): sample the queue depth during the
             * paused load.  A paused SDL device must not consume the queue -- if q shrinks
             * across these lines the open-paused assumption is broken (or something cleared
             * the queue); if it starts at 0 the prime never landed.  Bounded + log-only. */
            static int aqp_n = 0;
            if (g_log && (aqp_n % 100) == 0 && aqp_n < 10000) {
                fprintf(g_log, "AQ-PAUSED fr=%d q=%.2ff\n", g_cur_frame,
                        (double)SDL_GetQueuedAudioSize(g_audio_dev)/(double)frame_bytes);
                fflush(g_log);
            }
            aqp_n++;
            if (g_a_min < 0 || g_a_max > 0) {        /* sound has started (frame not pure silence) */
                SDL_PauseAudioDevice(g_audio_dev, 0); g_audio_paused = 0;
                if (g_log) { fprintf(g_log, "AUDIO-UNPAUSE fr=%d ic=%llu\n", g_cur_frame, (unsigned long long)g_icount); fflush(g_log); }
            }
        }
        if (!g_audio_paused && g_log) {   /* DIAG: re-verify the underrun fix -- is the queue still starving? */
            Uint32 qd = SDL_GetQueuedAudioSize(g_audio_dev);
            if (qd < (Uint32)(SMP_PER_FRAME*2*sizeof(int16_t))) {
                static int un=0; if (un++<4000) { fprintf(g_log, "AQ-UNDERRUN fr=%d q=%.2ff\n",
                    g_cur_frame, (double)qd/(double)(SMP_PER_FRAME*2*sizeof(int16_t))); fflush(g_log); }
            }
        }
        if (!g_audio_paused) {
            /* bound latency: stop queuing if the backlog is already deep (gameplay ~8
             * frames; attract deep-backlog mode, if enabled, uses g_av_delay_frames). */
            int cap_frames = (g_av_attract_audio && g_av_delay_frames > 0)
                           ? g_av_delay_frames + 1 : 8;
            if (SDL_GetQueuedAudioSize(g_audio_dev) < frame_bytes * (Uint32)cap_frames)
                SDL_QueueAudio(g_audio_dev, buf, frame_bytes);
        }
    }
    g_smp_done = 0;     /* start the next frame fresh */
}

static void custom_write(uint32_t off, uint16_t v) {
    off &= 0x1fe;
    /* set/clear semantics for DMACON/INTENA/INTREQ/ADKCON (bit15 = set/clear) */
    if (off==0x096 || off==0x09a || off==0x09c || off==0x09e) {
        uint16_t *r = &g_custom[off>>1];
        if (v & 0x8000) *r |= (v & 0x7fff); else *r &= ~(v & 0x7fff);
        if (off==0x09a || off==0x09c) update_ipl();
        if (off==0x096) {
            paula_update_dma();   /* (re)evaluate audio channel enables */
        }
    } else {
        g_custom[off>>1] = v;
    }
    /* Paula audio register writes: keep the host-rate step in sync when a period
     * is changed live (music players rewrite AUDxPER per note); LCH/LCL/LEN are
     * latched on DMA (re)start / loop, but reflecting period immediately matches
     * HW behaviour for held notes. */
    if (off >= 0x0a0 && off < 0x0e0) {
        int ch = (off - 0x0a0) / 0x10;
        int reg = (off - 0x0a0) % 0x10;
        if (reg == 0x06 && g_paula[ch].enabled) paula_recalc_step(ch);  /* AUDxPER */
        if (reg == 0x04) g_len_armed[ch] = 1;  /* AUDxLEN (re)written: loop length now coherent w/ loc (screech fix) */
        /* PER-DROP watch (2026-06-25): a very low AUDxPER written to a LIVE audible channel
         * mid-note pitches it up into a possible "beep" -- an edge the enable/reload watches
         * miss.  Combat only, bounded.  Read-only diagnostic. */
        if (reg == 0x06 && g_paula[ch].enabled && g_log && g_os && !g_blt_busy_scope) {
            uint32_t pv = v & 0xffff, pvol = g_custom[(0x0a8 + ch*0x10)>>1] & 0x7f;
            static int perdrop_n = 0;
            if (pv && pv < 160 && pvol >= 24 && perdrop_n < 200) {
                fprintf(g_log, "PER-DROP AUD%d per=%u loc=%06x len=%u vol=%u pc=%06x ic=%llu\n",
                        ch, pv, (unsigned)g_paula[ch].loc, (unsigned)g_paula[ch].len, pvol,
                        (unsigned)m68k_get_reg(NULL, M68K_REG_PPC), (unsigned long long)g_icount);
                fflush(g_log); perdrop_n++;
            }
        }
        if (g_audlog && g_log && g_audlog_n < 40000) {
            static const char *rn[] = {"LCH","LCL","LEN","PER","VOL","DAT"};
            const char *nm = (reg < 12) ? rn[reg/2] : "?";
            /* Suppress unchanged-value PER rewrites: the music driver rewrites
             * AUDxPER every tick with the same period, which would flood the
             * (bounded) audlog and bury the SFX trigger events at later scene
             * beats.  Log a PER write only when its value actually changes. */
            static uint16_t last_per[4] = {0xffff,0xffff,0xffff,0xffff};
            int skip = (reg == 0x06 && v == last_per[ch]);
            if (reg == 0x06) last_per[ch] = v;
            if (!skip) {
                fprintf(g_log, "    AUD%d %s <= %04x pc=%06x fr=%d ic=%llu\n", ch, nm, v,
                        (unsigned)m68k_get_reg(NULL, M68K_REG_PPC), g_cur_frame, (unsigned long long)g_icount);
                g_audlog_n++;
            }
        }
    }
    if (g_log && g_custw_log < 4000) {
        fprintf(g_log, "    CUSTW %-8s(%03x) <= %04x\n", creg_name(off), off, v);
        g_custw_log++;
    }
    /* --delvlog (TEMP): trace palette writes (COLOR00..31, off 0x180..0x1bf) when the value
     * actually changes, with the writer PC -- to find which routine loads the wrong delivery
     * palette over the still-displayed map bitplanes. */
    if (g_delvlog && g_log && off >= 0x180 && off < 0x1c0) {
        static uint16_t last_col[32] = {0};
        int ci = (int)((off - 0x180) >> 1);
        if (v != last_col[ci]) {
            last_col[ci] = v;
            fprintf(g_log, "  PALW color%-2d <= %03x pc=%06x fr=%d ic=%llu scene=%u\n",
                    ci, v & 0xfff, (unsigned)m68k_get_reg(NULL, M68K_REG_PPC),
                    g_cur_frame, (unsigned long long)g_icount, (unsigned)r32(0x2fb1cu));
        }
    }
    /* DSKLEN ($024): start trackdisk DMA on the SECOND identical write with
     * DMAEN(bit15) set and a nonzero word count, provided DSKEN(DMACON bit4) is
     * on.  (Amiga requires writing DSKLEN twice to start; the game writes
     * 0x4000 then 0xa800 0xa800.)  Reads from DSKPT ($020/$022) into chip RAM. */
    if ((off==0x080 || off==0x082 || off==0x088) && g_rdbg && g_log) {
        static uint32_t last_cop=0xffffffff;
        uint32_t c = (((uint32_t)g_custom[0x080>>1]<<16)|g_custom[0x082>>1]);
        if (c!=last_cop) { fprintf(g_log,"COP1LC<=%06x (via %03x) pc=%06x ic=%llu\n", c, off, (unsigned)m68k_get_reg(NULL,M68K_REG_PC), (unsigned long long)g_icount); last_cop=c; }
    }
    if (off == 0x024) {
        static uint16_t prev_dsklen = 0;
        int count = v & 0x3fff;
        if ((v & 0x8000) && count && v == prev_dsklen && (g_custom[0x096>>1] & 0x0010)) {
            uint32_t dest = (((uint32_t)g_custom[0x020>>1] << 16) | g_custom[0x022>>1]) & 0x1ffffe;
            trackdisk_dma(dest, count);
            prev_dsklen = 0;        /* consumed; require a fresh double-write */
        } else {
            prev_dsklen = v;
        }
    }
    /* BLTSIZE write triggers a blit (instant). Requires BLTEN (DMACON bit6). */
    if (off==0x058 && (g_custom[0x096>>1] & 0x0040)) blitter_run();
}

/* --------------------------------------------------------- bus dispatch */
static uint32_t g_watch = 0;   /* watch writes to this address (0=off) */
static uint32_t g_watchrd = 0; /* watch READS of this address: log each distinct reader PC (--watchrd; dev) */
uint64_t g_dumpat = 0; const char *g_dumpat_path = NULL;
static inline uint8_t  r8(uint32_t a)  { return g_ram[a & (RAM_SIZE-1)]; }
static inline uint16_t r16(uint32_t a) { a &= (RAM_SIZE-1); return (uint16_t)((g_ram[a]<<8)|g_ram[a+1]); }
static inline uint32_t r32(uint32_t a) { a &= (RAM_SIZE-1); return ((uint32_t)g_ram[a]<<24)|((uint32_t)g_ram[a+1]<<16)|((uint32_t)g_ram[a+2]<<8)|g_ram[a+3]; }
/* SOUND-ENGINE CODE-WRITE GUARD (fix for the 2026-06-18 loot/equip crash).
 *
 * Background: hovering a black knight's "blue orb" item in the loot/equip screen
 * crashed with ILLEGAL opcode 0x000a @pc=0x043496 -- a single byte at 0x43497
 * had changed 06->0a, mangling the sub.b operand of the instruction at 0x43494
 * (`sub.b $6(a3),d0` -> `$a(a3)`), so the persistent Mog sound server faulted the
 * next time it executed that instruction.  i.e. a stray/OOB WRITE clobbered the
 * audio engine's CODE (over-write theme; the over-read counterpart is the screech).
 *
 * The Mog sound engine occupies 0x42fd4..~0x43e00: the low part (0x42fd4..0x43224)
 * is its four per-voice DATA control blocks (bases 0x42fd4/0x43068/0x430fc/0x43190,
 * stride 0x94, selected by a fixed lea ladder @0x4333c -- it cannot pick a 5th
 * voice), and 0x43224..~0x43e00 is pure executable CODE.  Static RE shows the
 * loot/equip item handling in the original game is fully bounds-clamped (item
 * counts clamped to <=4, special-item bitmasks, the glyph blitter 0x3e5dc bounds
 * the sprite index against the 50-entry table at 0x104baa), so correctly-behaving
 * game logic NEVER writes into this code.  A WATCH across two full combat runs
 * (>300M instructions, many fights) confirmed: the ONLY writer into the code
 * region is the one-time module loader (pc=0x75aa4, ends ic~69.96M at Mog init);
 * after that, every legitimate sound-engine self-write targets only voice DATA
 * (< 0x43224).  So any write into 0x43224..0x43e00 once the engine is running is
 * the bug -- a wild pointer corrupting code.  A wild write must never silently
 * corrupt the program, so we GUARD it: once the engine is loaded, a store into
 * the code bytes is DROPPED and LOGGED (with the writing PC) instead of executed.
 * The audio code stays intact, the game keeps running, and the log pinpoints the
 * exact corrupting store for a root-cause fix of the source path.
 *
 * SCOPE: code bytes only (data + the rest of chip RAM are untouched); armed only
 * after the engine's voice dispatcher first runs (g_snd_loaded, set in
 * moon_instr_hook @0x4333c) so the loader populates the region freely.  When the
 * engine is not loaded (intro/cracktro) the guard is inert. */
#define SNDCODE_LO 0x43224u    /* first executable byte after the 4 voice DATA blocks */
#define SNDCODE_HI 0x43bbau    /* last code byte is the `rts` @0x43bb8..9; 0x43bbc+ is engine DATA
                                * (the "music playing" flag @0x43bbc, music-enable @0x43dc4, and a
                                * period/rate table) which the engine legitimately writes -- so the
                                * guarded CODE window is [0x43224, 0x43bba). */
static int g_snd_loaded = 0;   /* armed once the sound engine is running (see hook @0x4333c) */
static int g_sndguard_hits = 0;
static uint32_t g_curwr_pc = 0;       /* FREEZE diag: PPC of the last writer of the menu cursor 0x392d4 */
static uint32_t g_curwr_n = 0;        /* FREEZE diag: count of writes to 0x392d4 */
static int g_msleak_blocked = 0;  /* count of enemy/Guardian Moonstone assignments suppressed (see hook) */
/* Returns 1 if the write should be DROPPED (a stray store into live sound code). */
static int sndcode_guard(uint32_t a, uint32_t v, int sz) {
    if (!g_snd_loaded) return 0;
    if (a < SNDCODE_LO || a >= SNDCODE_HI) return 0;
    if (g_sndguard_hits < 200) {
        fprintf(g_log?g_log:stderr,
                "*** SNDCODE-GUARD blocked stray write @%06x <= %0*x (sz%d) pc=%06x ppc=%06x ic=%llu\n",
                a, sz*2, v, sz,
                (unsigned)m68k_get_reg(NULL,M68K_REG_PC),
                (unsigned)m68k_get_reg(NULL,M68K_REG_PPC),
                (unsigned long long)g_icount);
    }
    g_sndguard_hits++;
    return 1;
}
/* CORRUPT-WATCH (diagnostic 2026-06-21): the double-swing overflow sprays garbage into the
 * per-vblank task list / control block at 0x3c0ba.. (0x3c0ba is also the
 * stack-switch flag).  The list normally terminates BEFORE 0x3c0ba, so writes into [0x3c0ba,
 * 0x3c0c4) are the overflow (plus the rare legit flag-set at 0x3bb7c, value 1).  Log the writer
 * PC to trace the overflow to its SOURCE = the true root.  Capped. */
static void dump_derail(const char *why, unsigned pc, int force);  /* fwd (defined below) */
static int  save_state(const char *path);                          /* fwd (derail trap auto-snapshot) */
/* LIST-WRITE probe (2026-06-21): find the callback-list APPEND/registration routine -- the
 * PRODUCER.  Logs the distinct PCs that write the 9-slot task buffer [0x3c096,0x3c0ba) so we can
 * fix the overflow at its source (why the list ever ends without a terminator) rather than
 * bounding the consumer (the dispatcher).  Exercised by any screen-effect task registration. */
static inline void corrupt_watch(uint32_t a, uint32_t v, int sz) {
    if (!g_snd_loaded || a < 0x3c096u || a >= 0x3c0bau) return;
    static uint32_t seen[24]; static int sn = 0;
    uint32_t pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
    for (int k = 0; k < sn; k++) if (seen[k] == pc) return;     /* one line per distinct writer PC */
    if (sn < 24 && g_log) {
        seen[sn++] = pc;
        fprintf(g_log, "LIST-WRITE @%06x <= %0*x pc=%06x ppc=%08x ic=%llu\n",
                a, sz*2, v, pc, (unsigned)m68k_get_reg(NULL,M68K_REG_PPC), (unsigned long long)g_icount);
        fflush(g_log);
    }
}
/* LIVES-WATCH (diagnostic; WIDENED 2026-06-24 for the random-life-loss recurrence): log every
 * writer of ANY of the 4 knight records' lives byte (record+0x49) AND the "+0x82" lose-a-life
 * flag (record+0x82).  Roster = 4 records at 0x2e7dc, stride 0x84.  Previously watched only
 * rec0 (0x2e825) -- so a loss on a knight in rec1-3 was invisible (that's why the operator's
 * log showed no real decrement).  The logged rec#/old->v/PC names the source: a legit combat
 * death writes from pc=0x21434/0x21446; a stray/wild write (random loss) shows a different PC.
 * Read-only, capped.  --- after a repro, grep moonstone.log for LIVES-WATCH. */
static int g_lives_wn = 0;
static inline void lives_watch(uint32_t a, uint32_t v, int sz) {
    if (!g_os) return;
    uint32_t end = a + (uint32_t)sz;
    for (int i = 0; i < 4; i++) {
        uint32_t rec = 0x2e7dcu + (uint32_t)i*0x84u;
        uint32_t la = rec + 0x49u;          /* lives byte  */
        uint32_t fa = rec + 0x82u;          /* +0x82 "lose a life next turn-tick" flag */
        int lives = (a <= la && end > la);
        int flag  = (a <= fa && end > fa);
        if (!lives && !flag) continue;
        if (g_lives_wn++ < 200 && g_log) {
            fprintf(g_log, "LIVES-WATCH rec%d %s old=%02x sz%d v=%0*x pc=%06x ppc=%08x ic=%llu\n",
                    i, lives ? "lives" : "flag82",
                    g_ram[lives ? la : fa], sz, sz*2, v,
                    (unsigned)m68k_get_reg(NULL,M68K_REG_PC),
                    (unsigned)m68k_get_reg(NULL,M68K_REG_PPC), (unsigned long long)g_icount);
            fflush(g_log);
        }
    }
}
/* MOONSTONE-LEAK SETTER WATCH (2026-06-25): log any NON-ZERO write that lands the Moonstone in an
 * enemy/Guardian (slot 4/5) -- the COUNT field (rec+0x4e) or the token bitfield ([rec+0x60]+0x16).
 * The operator hit a black knight holding it on a FRESH game, so a live leak path the root-fix guards
 * miss is writing it; this names the writer PC.  v==0 writes (the per-frame scrub's clears) are
 * skipped, and the address early-out keeps it off the hot path.  Read-only (logging). */
static int g_mswatch_n = 0;
static inline void moonstone_watch(uint32_t a, uint32_t v, int sz) {
    if (!g_os || v == 0u) return;
    if (a < 0x2e7dcu || a >= 0x2eb00u) return;          /* records + quest-struct region only */
    uint32_t end = a + (uint32_t)sz;
    for (int i = 0; i < 4; i++) {
        uint32_t rec = 0x2e7dcu + (uint32_t)i*0x84u;
        uint32_t slot = r32(rec + 0x36u);
        if (slot != 4u && slot != 5u) continue;          /* only enemies (4) / Guardian (5) */
        uint32_t cnt = rec + 0x4eu;
        uint32_t inv = r32(rec + 0x60u);
        uint32_t tok = (inv >= 0x2e7dcu && inv < 0x2f000u) ? (inv + 0x16u) : 0xffffffffu;
        int hc = (a <= cnt && end > cnt);
        int ht = (tok != 0xffffffffu) && (a <= tok && end > tok);
        if ((hc || ht) && g_log && g_mswatch_n++ < 24)
            fprintf(g_log, "MS-LEAK-SET rec%d slot=%u %s <= %0*x pc=%06x ppc=%08x ic=%llu\n",
                    i, slot, hc ? "count(+0x4e)" : "token(+0x16)", sz*2, v,
                    (unsigned)m68k_get_reg(NULL,M68K_REG_PC),
                    (unsigned)m68k_get_reg(NULL,M68K_REG_PPC), (unsigned long long)g_icount), fflush(g_log);
    }
}
/* ROTWATCH (always-on, bounded): catch the in-combat "character disappears" regression.
 * Forensics on the broken save show the active-player pointer [0x2ebd0] went OFF-ROSTER
 * (0x134692 vs a valid 0x2e7dc+i*0x84 record) and the turn/player index [0x2ebc8]/[0x2f9da]
 * went out of range (=6, valid ~0-4).  Log the writing PC the instant any of those takes a
 * bad value, so we can NAME the culprit (a guard? the REST/skip-turn path? engine rotation
 * arith?) instead of guessing.  g_os-gated, bounded; off the hot path for normal values. */
static int g_rotwatch_n = 0;
static inline void rotwatch(uint32_t a, uint32_t v, int sz) {
    if (!g_os || !g_log || g_rotwatch_n >= 1000) return;
    const char *what = 0;
    /* NOTE: [0x2ebd0] (active-player ptr) legitimately cycles through every combat actor
     * each turn -- "off-roster" there is NORMAL (verified: fired 1000x across 3 distinct
     * actor ptrs with no vanish), so we do NOT watch it.  The only clean signal is the
     * PLAYER's own actor-slot field [0x2ee7a]: in a good fight it holds the player roster
     * record 0x2e7dc and never anything else (0 false positives).  A non-roster, non-null
     * write to it = the actual player-actor corruption (the vanish).  Catch its PC. */
    if (a==0x2ee7au && sz==4 && v != 0 && v != 0x2e7dcu) what = "PLAYERSLOT[2ee7a]<-non-roster (VANISH ROOT)";
    if (what) {
        fprintf(g_log, "ROTWATCH %s <= %x sz%d ppc=%06x pc=%06x ic=%llu\n", what, v, sz,
                (unsigned)m68k_get_reg(NULL,M68K_REG_PPC), (unsigned)m68k_get_reg(NULL,M68K_REG_PC),
                (unsigned long long)g_icount);
        fflush(g_log); g_rotwatch_n++;
    }
}
static inline void w8(uint32_t a, uint8_t v)  {
    a &= (RAM_SIZE-1); corrupt_watch(a, v, 1); lives_watch(a, v, 1); moonstone_watch(a, v, 1); rotwatch(a, v, 1);
    if (g_watch && a>=(g_watch-2) && a<=(g_watch+4))
        fprintf(g_log?g_log:stderr,"  WATCH8 @%06x <= %02x pc=%06x ic=%llu\n", a, v, (unsigned)m68k_get_reg(NULL,M68K_REG_PC), (unsigned long long)g_icount);
    if (sndcode_guard(a, v, 1)) return;
    g_ram[a] = v; }
static inline void w16(uint32_t a, uint16_t v){ a &= (RAM_SIZE-1); corrupt_watch(a, v, 2); lives_watch(a, v, 2); moonstone_watch(a, v, 2); rotwatch(a, v, 2);
    if (a == 0x392d4u) { g_curwr_pc = (uint32_t)m68k_get_reg(NULL,M68K_REG_PPC); g_curwr_n++; }
    if (g_watch && a>=(g_watch-2) && a<=(g_watch+4))
        fprintf(g_log?g_log:stderr,"  WATCH16 @%06x <= %04x pc=%06x ic=%llu\n", a, v, (unsigned)m68k_get_reg(NULL,M68K_REG_PC), (unsigned long long)g_icount);
    if (sndcode_guard(a, v, 2)) return;
    g_ram[a]=(uint8_t)(v>>8); g_ram[a+1]=(uint8_t)v; }
static int g_novblank = 0;     /* deprecated --novblank: now a no-op (gate is default) */
static int g_pollonly = 0;     /* --pollonly: never inject VERTB (pure polling, debug) */
static int g_forcevblank = 0;  /* --forcevblank: inject VERTB every frame (debug) */
static inline void w32(uint32_t a, uint32_t v){
    a &= (RAM_SIZE-1); corrupt_watch(a, v, 4); lives_watch(a, v, 4); moonstone_watch(a, v, 4); rotwatch(a, v, 4);
    if (a == 0x392d4u) { g_curwr_pc = (uint32_t)m68k_get_reg(NULL,M68K_REG_PPC); g_curwr_n++; }
    if (g_watch && a==g_watch && g_log) fprintf(g_log,"  WATCH w32 @%06x <= %08x pc=%06x ic=%llu\n", a, v, (unsigned)m68k_get_reg(NULL,M68K_REG_PC), (unsigned long long)g_icount);
    if (sndcode_guard(a, v, 4)) return;
    g_ram[a]=(uint8_t)(v>>24); g_ram[a+1]=(uint8_t)(v>>16); g_ram[a+2]=(uint8_t)(v>>8); g_ram[a+3]=(uint8_t)v; }

static int in_ram(uint32_t a)    { return a < RAM_SIZE; }
static int in_custom(uint32_t a) { return a >= CUSTOM_LO && a < CUSTOM_HI; }
static int in_cia(uint32_t a)    { return a >= CIA_LO && a < CIA_HI; }

static int g_fire = 0;   /* 1 = left fire/mouse button held */

/* ---- CIA 8520 model (timers A/B + ICR) ---- */
typedef struct {
    uint8_t  reg[16];
    uint16_t ta_latch, tb_latch;   /* reload values */
    uint16_t ta, tb;               /* live counters */
    uint8_t  icr_flags;            /* pending (read-clears): bit0=TA bit1=TB ... */
    uint8_t  icr_mask;             /* enabled sources */
    int      eclk_accum;           /* CPU-cycle->E-clock remainder */
} Cia;
static Cia g_ca, g_cb;

static void cia_step(Cia *c, int cpu_cycles) {
    int e = c->eclk_accum + cpu_cycles;
    int ticks = e / 10;            /* E-clock = CPU/10 */
    c->eclk_accum = e % 10;
    /* Timer A: CRA(reg14) bit0=START bit3=ONESHOT */
    if (c->reg[14] & 0x01) {
        int rem = ticks;
        while (rem > c->ta) {
            rem -= (c->ta + 1);
            c->icr_flags |= 0x01;
            if (c->reg[14] & 0x08) { c->reg[14] &= ~0x01; c->ta = c->ta_latch; rem = 0; break; }
            c->ta = c->ta_latch;
        }
        c->ta -= rem;
    }
    if (c->reg[15] & 0x01) {       /* Timer B: CRB(reg15) */
        int rem = ticks;
        while (rem > c->tb) {
            rem -= (c->tb + 1);
            c->icr_flags |= 0x02;
            if (c->reg[15] & 0x08) { c->reg[15] &= ~0x01; c->tb = c->tb_latch; rem = 0; break; }
            c->tb = c->tb_latch;
        }
        c->tb -= rem;
    }
}
static void cia_tick(int cpu_cycles) { cia_step(&g_ca, cpu_cycles); cia_step(&g_cb, cpu_cycles); }

static uint8_t cia_rd(Cia *c, int r, int is_a) {
    switch (r) {
        case 0:
            if (is_a) { /* PRA: bit5 /RDY=0 (drive ready), bit4 /TRK0 asserted(0)
                           iff the modelled head is at cylinder 0 (so the
                           recalibrate loop at 0x29b44 stops there exactly),
                           bit2 /CHNG=1 (no change), fire active-low */
                uint8_t v=0xff; v&=~0x20;
                if(g_os && g_dsk_cyl==0) v&=~0x10;   /* /TRK0 low when at cyl 0 */
                if(g_os && g_chng_low) v&=~0x04;     /* /CHNG low after a disk swap (until a step) */
                if(g_fire)v&=~0x40; if(g_fire2)v&=~0x80;
                if(g_inlog) inlog("PRA-FIRE", v);
                return v; }
            return 0xff;
        case 4: return c->ta & 0xff;
        case 5: return c->ta >> 8;
        case 6: return c->tb & 0xff;
        case 7: return c->tb >> 8;
        case 13: { uint8_t f=c->icr_flags; if (f & c->icr_mask) f|=0x80; c->icr_flags=0; return f; }
        default: return c->reg[r];
    }
}
static void cia_wr(Cia *c, int r, uint8_t v) {
    switch (r) {
        case 4: c->ta_latch=(c->ta_latch&0xff00)|v; break;
        case 5: c->ta_latch=(c->ta_latch&0x00ff)|(v<<8); if(!(c->reg[14]&0x01)) c->ta=c->ta_latch; break;
        case 6: c->tb_latch=(c->tb_latch&0xff00)|v; break;
        case 7: c->tb_latch=(c->tb_latch&0x00ff)|(v<<8); if(!(c->reg[15]&0x01)) c->tb=c->tb_latch; break;
        case 13: if (v&0x80) c->icr_mask|=(v&0x7f); else c->icr_mask&=~(v&0x7f); break;
        case 14: c->reg[14]=v; if (v&0x10) c->ta=c->ta_latch; break;   /* LOAD */
        case 15: c->reg[15]=v; if (v&0x10) c->tb=c->tb_latch; break;
        default: c->reg[r]=v; break;
    }
}

/* Model the floppy drive control written through CIA-B PRB ($BFD100):
 *   bit0 /STEP  : a head step happens on the high->low transition of /STEP,
 *                 in the direction given by DIR, addressed to the selected drive.
 *   bit1 DIR    : 1 = step toward cylinder 0 (decrement), 0 = step out (increment).
 *   bit2 /SIDE  : 1 => head 0, 0 => head 1 (per program code at 0x29328).
 *   bit3-6 /SEL0-3: drive select, active low (0 = selected).
 * The selected drive is tracked for completeness; only drive 0 carries a disk. */
static void disk_prb_write(uint8_t v) {
    /* drive select (active low): pick the lowest selected unit */
    int sel = -1;
    for (int d = 0; d < NDRIVE; d++) if (!((v >> (3+d)) & 1)) { sel = d; break; }
    g_dsk_sel = sel;
    /* Side select: $BFD100 bit2 (1 => head 0, 0 => head 1), the live /SIDE line. */
    g_dsk_head = ((v >> 2) & 1) ? 0 : 1;
    /* step pulse: high->low edge on bit0 steps the head of the selected drive */
    int prev_step = (g_prb_prev >> 0) & 1;
    int cur_step  = (v >> 0) & 1;
    /* Step the head on the high->low /STEP edge for WHICHEVER drive is selected.
     * Moonstone's front-end loader (`program`) drives df0: (sel==0), but the Mog
     * engine recalibrates with df1: selected (sel==1, $bfd100 bit4=/SEL1 low).
     * We model a single disk that the host serves to whatever drive is active,
     * so tracking g_dsk_cyl for any selected unit lets Mog's seek-to-track-0
     * (which polls CIA-A PRA bit4 /TRK0 = g_dsk_cyl==0) actually terminate. */
    if (sel >= 0 && prev_step && !cur_step) {
        int dir = (v >> 1) & 1;            /* 1 = toward cyl 0 */
        if (dir) { if (g_dsk_cyl > 0)  g_dsk_cyl--; }
        else     { if (g_dsk_cyl < 79) g_dsk_cyl++; }
        g_chng_low = 0;   /* a head step on the new disk clears /CHNG (disk now valid) */
    }
    g_prb_prev = v;
}

/* CIA register select = address bits 8..11; CIA-A base $BFE001, CIA-B $BFD000. */
static uint8_t cia_read(uint32_t a) {
    if ((a & 0xfff0ff) == 0xbfe001) return cia_rd(&g_ca, (a>>8)&15, 1);
    if ((a & 0xfff0ff) == 0xbfd000) return cia_rd(&g_cb, (a>>8)&15, 0);
    return 0xff;
}
static void cia_write(uint32_t a, uint8_t v) {
    if ((a & 0xfff0ff) == 0xbfe001) cia_wr(&g_ca, (a>>8)&15, v);
    else if ((a & 0xfff0ff) == 0xbfd000) {
        int r = (a>>8)&15;
        if (g_os && r == 1) disk_prb_write(v);   /* PRB = drive control */
        cia_wr(&g_cb, r, v);
    }
}

static int g_memlog = 0;   /* --memlog: log unmapped bus accesses (off by default = quiet) */
static void unmapped(uint32_t a, int wr, uint32_t v, int sz) {
    (void)v;
    g_unmapped++;
    /* Quiet + benign by default: unmapped reads return 0/0xff (floating-bus-like).
     * These are harmless (e.g. the enemy AI reads an uninitialised +0x60 behaviour
     * pointer = a garbage high address; the read yields 0 and the AI defaults).
     * No halt in normal play — genuine runaways still trip the JUMP-TO-LOW /
     * illegal-instruction guards, and headless runs are bounded by --frames. */
    if (g_memlog && g_log && g_unmapped < 8000)
        fprintf(g_log, "    UNMAPPED %s%d @ %06x pc=%06x\n", wr?"W":"R", sz*8, a,
                (unsigned)m68k_get_reg(NULL, M68K_REG_PC));
}

/* -------------------------------------------------- Musashi memory hooks */
unsigned int m68k_read_memory_8(unsigned int a) {
    if (in_ram(a)) return r8(a);
    if (in_custom(a)) { uint16_t w=custom_read(a-CUSTOM_LO); return (a&1)?(w&0xff):(w>>8); }
    if (in_cia(a)) return cia_read(a);
    unmapped(a,0,0,1); return 0xff;
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if (g_watchrd && a == g_watchrd && g_log) {   /* dev: log each distinct PC that reads the watched word */
        static uint32_t seen[32]; static int sn = 0; uint32_t pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PPC);
        int known = 0; for (int k = 0; k < 32; k++) if (seen[k] == pc) { known = 1; break; }
        if (!known) { seen[sn & 31] = pc; sn++;
            fprintf(g_log, "WATCH-RD @%06x pc=%06x val=%04x ic=%llu\n", a, pc, r16(a), (unsigned long long)g_icount);
            fflush(g_log); }
    }
    if (in_ram(a)) return r16(a);
    if (in_custom(a)) return custom_read(a-CUSTOM_LO);
    if (in_cia(a)) return (cia_read(a)<<8)|cia_read(a+1);
    unmapped(a,0,0,2); return 0xffff;
}
unsigned int m68k_read_memory_32(unsigned int a) {
    if (g_watchrd && a == g_watchrd && g_log) {   /* dev: log each distinct PC that reads the watched long */
        static uint32_t seen[32]; static int sn = 0; uint32_t pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PPC);
        int known = 0; for (int k = 0; k < 32; k++) if (seen[k] == pc) { known = 1; break; }
        if (!known) { seen[sn & 31] = pc; sn++;
            fprintf(g_log, "WATCH-RD32 @%06x pc=%06x val=%08x ic=%llu\n", a, pc, (unsigned)r32(a), (unsigned long long)g_icount);
            fflush(g_log); }
    }
    if (in_ram(a)) return r32(a);
    if (in_custom(a)) return (custom_read(a-CUSTOM_LO)<<16)|custom_read(a-CUSTOM_LO+2);
    if (in_cia(a)) return (cia_read(a)<<24)|(cia_read(a+1)<<16)|(cia_read(a+2)<<8)|cia_read(a+3);
    unmapped(a,0,0,4); return 0xffffffff;
}
void m68k_write_memory_8(unsigned int a, unsigned int v) {
    if (in_ram(a)) { w8(a,(uint8_t)v); return; }
    if (in_custom(a)) {
        uint32_t off=(a-CUSTOM_LO)&0x1fe; uint16_t cur=g_custom[off>>1];
        /* 68000 byte-write data duplication: a move.b drives the SAME byte on both
         * halves of the data bus (D0-D7 and D8-D15).  For the word-only AUDxVOL regs
         * Paula latches the volume from the low 7 bits, so a byte write to the EVEN
         * (high-byte) address still sets the volume on real hardware.  Moonstone's
         * music driver relies on exactly this: the C-command volume handler at 0xa04
         * does `move.b vol,($8,A5)` (e.g. move.b to $dff0d8).  Modelling the write as
         * high-byte-only left the volume bits at the stale instrument default (0x40 =
         * full), so every set-volume note played full -- inflating soft layers like
         * the intro trumpet's quiet echo (song vol 0x10) into an audible doubling.
         * Duplicate the byte for the AUDxVOL regs (0x0a8/0x0b8/0x0c8/0x0d8) so the
         * volume lands in the low bits, matching hardware.  Scoped to VOL to stay
         * determinism-neutral (VOL is write-only; the CPU never reads it back). */
        if (off >= 0x0a0 && off < 0x0e0 && (off & 0x0f) == 0x08) {
            uint16_t b=(uint16_t)(v&0xff); cur=(uint16_t)(b|(b<<8));
        } else if (a&1) cur=(cur&0xff00)|(v&0xff);
        else cur=(cur&0x00ff)|((v&0xff)<<8);
        custom_write(off,cur); return;
    }
    if (in_cia(a)) { cia_write(a,(uint8_t)v); return; }
    unmapped(a,1,v,1);
}
void m68k_write_memory_16(unsigned int a, unsigned int v) {
    if (in_ram(a)) { w16(a,(uint16_t)v); return; }
    if (in_custom(a)) { custom_write(a-CUSTOM_LO,(uint16_t)v); return; }
    if (in_cia(a)) { cia_write(a,v>>8); cia_write(a+1,v&0xff); return; }
    unmapped(a,1,v,2);
}
void m68k_write_memory_32(unsigned int a, unsigned int v) {
    if (in_ram(a)) { w32(a,v); return; }
    if (in_custom(a)) { custom_write(a-CUSTOM_LO,v>>16); custom_write(a-CUSTOM_LO+2,v&0xffff); return; }
    if (in_cia(a)) { cia_write(a,v>>24);cia_write(a+1,v>>16);cia_write(a+2,v>>8);cia_write(a+3,v); return; }
    unmapped(a,1,v,4);
}
/* disassembler reads (same space) */
unsigned int m68k_read_disassembler_8 (unsigned int a){ return m68k_read_memory_8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a){ return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a){ return m68k_read_memory_32(a); }

/* ------------------------------------------------------------- CPU hooks */
static int      g_flow = 0;             /* log PC page transitions */
static uint32_t g_last_page = 0xffffffff;
static uint64_t g_trace_from = 0;       /* begin per-instruction trace at this icount */
static int      g_trace_budget = 4000;  /* instrs to trace once g_trace_from hits (--tracen) */
static int      g_traceregs = 0;        /* dump extra registers in trace */
static int      g_os = 0;               /* AmigaOS HLE enabled */
static void     hle_dispatch(uint32_t pc);  /* fwd */
static void     hle_load_by_name(void);     /* fwd: traps nb 0x2ca94 */
static void     hle_stream_read(void);      /* fwd: traps nb 0x2cda0 */
static void     hle_stream_skip(void);      /* fwd: traps nb 0x2cf06 */
/* nb's loader hunk (the OFS file reader) is shared verbatim by `program`, but
 * relocated to a different base, so program calls the SAME routines at shifted
 * addresses (delta -0x1cd8). Trap both copies so program's Mog/asset loads are
 * served too. (If a future module relocates them elsewhere, add its trio here.) */
#define NB_LOAD_BY_NAME 0x0002ca94u
#define NB_STREAM_READ  0x0002cda0u
#define NB_STREAM_SKIP  0x0002cf06u
#define PR_LOAD_BY_NAME 0x0002adbcu
#define PR_STREAM_READ  0x0002b0c8u
#define PR_STREAM_SKIP  0x0002b22eu

static uint32_t g_prev_pc = 0;
static int      g_maplog = 0;         /* --maplog: log overland-map node-entry path (token pos, candidate list, fire-action, scene transitions) */
static int      g_cur_frame = 0;      /* current host frame (for diagnostics) */
static uint32_t g_disp_base = 0;      /* last render: the display base actually read (post-recovery) */
static uint32_t g_l3trace = 0;        /* --l3trace ADDR: log A7/SR each time PC hits ADDR (IRQ-entry stack check) */
static uint32_t g_l3trace_n = 0;
static int      g_program_served = 0; /* set once nb's loader has streamed `program` */
static int      g_pools_relocated = 0;/* one-shot: program's mem pools re-based */

/* ---- DEBUG-ONLY memory poke (--poke), gated on g_os + off by default ----
 * Lets a test set up end-game state quickly (e.g. the player's Moonstone count
 * at player_rec+0x4e, the Valley-key gate, or a map position) so the win path
 * can be exercised without a long real-time playthrough.  Each poke is applied
 * ONCE, when PC first reaches its gate address (default 0x21206 = program's main
 * dispatch loop, which runs continuously once the player record exists).  This
 * is purely a verification aid and is completely inert unless --poke is given. */
#define MAX_POKES 16
typedef struct { uint32_t addr; uint32_t val; int size; uint32_t at_pc; int applied; } Poke;
static Poke g_pokes[MAX_POKES];
static int  g_npokes = 0;
/* --- CRASH/DERAIL forensics (for the 2-troll simultaneous-overhead-swing bug,
 * which ends in the 68k running off into garbage = "runaway"): a rolling ring of
 * recent PCs + a snapshot (regs / stack call-chain) taken the instant the PC lands
 * in a definitely-not-code region.  The ring shows the path INTO the derail and the
 * stack's return addresses point back to the code that made the bad jump. --- */
static uint32_t g_pcring[128];
static uint32_t g_pcring_i = 0;
static int      g_derail_n = 0;
static int      g_tasklist_fix = 1;   /* ROOT FIX 2026-06-21: per-vblank task-list integrity -- idempotent
                                       * screen-effect (0x2a10c) registration + effect-only removal.  Fixes
                                       * the two-troll crash AND the cursor-menu freeze.  Hardened 2026-07-02
                                       * (audit): opcode-guarded hooks, value-based cursor removal, bounded
                                       * divergence log.  --notaskfix A/B disables the whole set. */
static int      g_taskhook_n = 0;     /* bounded TASKFIX divergence-log line count */
/* Compact-remove one slot from the per-vblank task list [0x3c096,0x3c0ba): shift
 * later entries down one, zero-fill the tail.  The dispatcher (0x3b906) stops at
 * the FIRST NULL slot, so a hole in the middle would silently disable every
 * handler after it (incl. the Mog sound tick 0x43316) -- compaction keeps the
 * list contiguous.  Returns 1 if entries AFTER the slot existed (their addresses
 * SHIFTED -- the one case where a saved-slot pointer elsewhere goes stale; every
 * remover below is value-based, so staleness self-heals, and we log it). */
static int tasklist_compact(uint32_t slot) {
    int shifted = 0;
    uint32_t s = slot;
    while (s + 4u < 0x3c0bau) {
        uint32_t nxt = r32(s + 4u);
        if (nxt) shifted = 1;
        w32(s, nxt);
        if (nxt == 0u) break;
        s += 4u;
    }
    if (s + 4u >= 0x3c0bau) w32(s, 0u);
    return shifted;
}
static int      g_aud1_dmafix = 1;    /* ROOT FIX 2026-06-30: the Mog sound-start routine (entry 0x4366c)
                                       * ends with a STRAY hard-coded `move.w #$2,DMACON` @0x4371c that clears
                                       * AUD1's DMA on EVERY voice (re)start, regardless of which channel is
                                       * being started -- every other custom-reg write in that routine is
                                       * channel-indexed (off A4); only this one is a literal AUD1 bit = a
                                       * copy/paste slip in the original game.  It chops any one-shot SFX that
                                       * happens to be on AUD1 (the "sound cut short" bug).  Fix = skip that one
                                       * 6-byte instruction (PC 0x4371c -> 0x43722).  --noaud1fix A/B. */
static int      g_choke_haul_fix = 1;  /* ROOT FIX 2026-07-01: the canopy-choke "haul to canopy" launch
                                       * (0x27278) builds the arc destination X as curX + 0x96 (+150) with an
                                       * UNCONDITIONAL rightward add (0x272a8 `addi.w #$96,(A0)+`) and no
                                       * facing/arena term; the arc integrator then writes actor.X unbounded.
                                       * On the 320-wide, non-scrolling combat arena a monkey grabbing the
                                       * player on the RIGHT half hauls off the edge (choker lands X=340) ->
                                       * invisible choker, choke SFX + 1-HP/tick drain from off-screen, escape
                                       * stab clipped away, player choked to death -> inventory.  Root fix:
                                       * swing the haul toward the arena INTERIOR (centre 160) so it can never
                                       * exit -- left half keeps +150 (unchanged), right half gets -150; only
                                       * the previously-off-screen case changes.  --nochokefix A/B. */
static int      g_taskdedup_n = 0;
/* (Removed 2026-06-25: g_lifeguard / the @0x260a4 +0x82 clear.  It was REDUNDANT -- the new-game
 * per-record init already clears +0x82 at 0x260d8 -- so it did nothing for the random life loss,
 * which is +0x82 getting corrupted DURING play, not left uninitialized.  See moonstone-open-bugs.) */
static int      g_penaltyset_n = 0;   /* WATCHDOG 2026-06-22: bounded log of the +0x82 "lose a life" setter
                                       * (0x264ce) firing in live play.  The orphaned-token load-guard was
                                       * removed once the operator re-saved clean (root assumed to be the
                                       * already-fixed memory-corrupting crashes); this probe stays so a
                                       * recurrence is caught at its source.  Inert unless 0x264ce executes. */
static uint32_t g_inread_n = 0;       /* FREEZE diag: count of input-routine (0x3bdec, mouse read) calls */
static uint32_t g_curupd_n = 0;       /* FREEZE diag: count of cursor-mover (0x2d624) runs -- dispatched? */
static uint32_t g_last_a1 = 0;        /* FREEZE diag: last callback the dispatcher jsr'd (0x3b91a) */
static uint32_t g_cursor_cb = 0;      /* FREEZE diag: the dispatch-list entry that reaches the cursor-mover */
static uint32_t g_cur_caller = 0;     /* FREEZE diag: immediate caller (return addr) of the cursor-mover */
static void dump_derail(const char *why, unsigned pc, int force) {
    if (!g_log || (!force && g_derail_n >= 6)) return;
    g_derail_n++;
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    fprintf(g_log, "=== DERAIL (%s) pc=%06x ic=%llu ===\n", why, pc, (unsigned long long)g_icount);
    fprintf(g_log, "  D %08x %08x %08x %08x %08x %08x %08x %08x\n",
        (unsigned)m68k_get_reg(NULL,M68K_REG_D0),(unsigned)m68k_get_reg(NULL,M68K_REG_D1),(unsigned)m68k_get_reg(NULL,M68K_REG_D2),(unsigned)m68k_get_reg(NULL,M68K_REG_D3),
        (unsigned)m68k_get_reg(NULL,M68K_REG_D4),(unsigned)m68k_get_reg(NULL,M68K_REG_D5),(unsigned)m68k_get_reg(NULL,M68K_REG_D6),(unsigned)m68k_get_reg(NULL,M68K_REG_D7));
    fprintf(g_log, "  A %08x %08x %08x %08x %08x %08x %08x sp=%08x\n",
        (unsigned)m68k_get_reg(NULL,M68K_REG_A0),(unsigned)m68k_get_reg(NULL,M68K_REG_A1),(unsigned)m68k_get_reg(NULL,M68K_REG_A2),(unsigned)m68k_get_reg(NULL,M68K_REG_A3),
        (unsigned)m68k_get_reg(NULL,M68K_REG_A4),(unsigned)m68k_get_reg(NULL,M68K_REG_A5),(unsigned)m68k_get_reg(NULL,M68K_REG_A6),(unsigned)sp);
    fprintf(g_log, "  stack:");
    for (int k=0;k<24;k++) fprintf(g_log, " %06x", (unsigned)(r32(sp + k*4) & 0xffffff));
    fprintf(g_log, "\n  pcring(old->new):");
    for (int k=0;k<128;k++) fprintf(g_log, " %06x", g_pcring[(g_pcring_i + k) & 127]);
    fprintf(g_log, "\n"); fflush(g_log);
}

void moon_instr_hook(unsigned int pc) {
    /* record recent PCs (derail forensics), but SKIP the line-245 raster-wait spin
     * (0x3fe12..0x3fe26) so the ring shows the surrounding loop, not just the wait. */
    if (pc < 0x3fe12u || pc > 0x3fe26u) { g_pcring[g_pcring_i & 127] = pc; g_pcring_i++; }
    /* "wait for fire" routine (0x22fd0) -- the Stonehenge moonstone-DELIVERY overview spins here
     * while it shows the map with a wrong (delivery) palette; the normal map turn-loop never does.
     * Flag it so render suppresses that garbled-map frame.  Cleared per-frame in capture_frame. */
    if (g_os && pc == 0x22fd0u) g_firewait_hot = 1;
    /* COMPOSE/FLIP TRACE (--cflog): flip entry 0x3fcd6 points the copper at [0xc0e0]
     * (back, about to be displayed) then swaps [0xc0dc]<->[0xc0e0].  Log the buffer that
     * is about to become the live front (= [0xc0e0] pre-swap) vs the current front. */
    if (g_cflog && g_log && pc == 0x3fcd6u)
        fprintf(g_log, "CF-FLIP fr=%d newfront=%06x curfront=%06x\n",
            g_cur_frame, r32(0xc0e0u), r32(0xc0dcu));
    /* MOONSTONE-DELIVERY garbled-map suppression + auto-forward (see g_delivery_win / g_map_pal above).
     * 0x22820 (matching-moonstone return) opens a generous frame WINDOW; render blacks out scene-9 frames
     * whose palette differs from the snapshotted good map palette while the window is open.  Also auto-
     * press the first map's fire-wait (jsr 0x22fd0 returns to 0x22890) so it forwards without a key-press
     * -- caller-gated so the disk-swap fire-wait (needs the autoswap settle) is left to autoswap. */
    if (g_os) {
        if (pc == 0x22820u) { g_delivery_win = 1200;   /* ~24s window; the palette test keeps it safe even if it overruns */
            g_winwait_frame = -1;                      /* reset the win-screen auto-advance timer */
            if (g_log) fprintf(g_log, "DELIVERY arm @22820 fr=%d ic=%llu scene=%u\n",
                               g_cur_frame, (unsigned long long)g_icount, (unsigned)r32(0x2fb1cu)); }
        /* "The End" (draw routine 0x221ca, descriptor 0x21176) no longer needs a per-screen
         * recovery opt-out: it is an in-game/outro screen (g_blt_busy_scope==0), so the
         * structural BACKBUF-RECOVER gate already excludes it.  (Was: g_theend_win pin.) */
        /* The win screen "You have completed the quest" (handler 0x22876) ends in a fire-wait at
         * 0x22fd0.  AUTO-ADVANCE it after ~3 s of display: the bug was pressing fire INSTANTLY, which
         * shoved the screen straight into the relaunch mid-composite (the "garbled random screen").
         * TIMING it instead lets the bg8.piv art render + sit for 3 s, THEN fires to continue into the
         * relaunch/credits -- so the outro still plays hands-free, but the screen is actually seen. */
        if (g_delivery_win > 0 && pc == 0x22fd0u && g_winwait_frame < 0
            && r32((uint32_t)m68k_get_reg(NULL, M68K_REG_A7)) == 0x22890u) {
            g_winwait_frame = g_cur_frame;            /* win-screen fire-wait just armed (entered once) */
        }
        if (g_winwait_frame >= 0 && g_delivery_win > 0) {  /* the loop polls fire internally, so drive it here */
            int el = g_cur_frame - g_winwait_frame;
            if (el >= 100 && el < 106) {              /* ~3 s of visible win screen: it is already shown ~1 s
                                                       * (~53 frames, the 0x3fdf0 pre-wait delay) before this
                                                       * fire-wait arms, so +~2 s here ~= 3 s total; pulse fire
                                                       * ~6 frames so the internal poll loop reads it + advances */
                g_fire2 = 1;
            }
        }
        if (g_delivery_win > 0 && g_log) {         /* trace scene changes through the delivery (live diag) */
            static uint32_t dlast = 0xffffffffu;
            uint32_t dsc = r32(0x2fb1cu);
            if (dsc != dlast) { dlast = dsc;
                fprintf(g_log, "DELIVERY scene->%u fr=%d ic=%llu disp=%06x\n",
                        dsc, g_cur_frame, (unsigned long long)g_icount, g_disp_base); }
        }
    }
    /* --delvlog (TEMP): trace the moonstone-delivery altar handler 0x21ca4 state machine + auto-fire
     * through every 0x22fd0 wait so the whole delivery (map overview -> disk swap -> map -> cutscene)
     * plays out headlessly and we can see the branch order + palette timing. */
    if (g_delvlog && g_os && g_log) {
        switch (pc) {
            case 0x21ca4u: fprintf(g_log, "DELV @21ca4 ENTER fr=%d ic=%llu f9f0=%04x e066=%02x scene=%u a1=%06x\n",
                    g_cur_frame, (unsigned long long)g_icount, (unsigned)r16(0x2f9f0u),
                    (unsigned)r8(0x2e066u), (unsigned)r32(0x2fb1cu), (unsigned)m68k_get_reg(NULL,M68K_REG_A1)); break;
            case 0x21cbau: fprintf(g_log, "DELV @21cba bsr 21d7a (cutscene-asset setup)\n"); break;
            case 0x21ce2u: fprintf(g_log, "DELV @21ce2 *** scene-9 MAP branch (e066 bit0 SET) -> shows the map overview ***\n"); break;
            case 0x21cfau: fprintf(g_log, "DELV @21cfa +1 MOONSTONE (e066 bit0 clear)\n"); break;
            case 0x21d00u: fprintf(g_log, "DELV @21d00 scene-2 CUTSCENE dispatch\n"); break;
            case 0x2bbecu: fprintf(g_log, "DELV scene-launch jsr 2bbec d0=%u fr=%d ic=%llu\n",
                    (unsigned)m68k_get_reg(NULL,M68K_REG_D0), g_cur_frame, (unsigned long long)g_icount); break;
            case 0x23feau: fprintf(g_log, "DELV @23fea disk prompt fr=%d ic=%llu namelow=%04x\n",
                    g_cur_frame, (unsigned long long)g_icount, (unsigned)r16(m68k_get_reg(NULL,M68K_REG_A0)+2)); break;
            case 0x22820u: fprintf(g_log, "DELV @22820 *** matching-moonstone RETURN branch (delivery latch arm) fr=%d ic=%llu ***\n",
                    g_cur_frame, (unsigned long long)g_icount); break;
            case 0x2289eu: fprintf(g_log, "DELV @2289e handler end (latch disarm) fr=%d ic=%llu\n",
                    g_cur_frame, (unsigned long long)g_icount); break;
            default: break;
        }
    }
    /* FREEZE diag: a hang in the line-245 raster-wait (operator's frozen-inventory bug).
     * If we spin at 0x3fe12 absurdly long without leaving, log the beam state so we can
     * see whether the beam is stuck or cycling-but-never-245. */
    { static uint32_t bw = 0;
      if (pc == 0x3fe12u) { bw++;
            if (bw == 400000u) dump_derail("BEAM-STUCK auto", pc, 1);   /* auto-snapshot the hang */
            if (bw % 400000u == 0 && g_log)
              fprintf(g_log, "BEAM-STUCK fc=%u line=%u dff006hi=%02x bw=%u ic=%llu\n",
                      g_frame_cycle, g_beam_line, (unsigned)(g_beam_line&0xff), bw,
                      (unsigned long long)g_icount), fflush(g_log); }
      else if (pc < 0x3fe10u || pc > 0x3fe26u) bw = 0;
    }
    /* FREEZE diag #2: the post-combat cursor-menu loop (frame loop 0x2bc30; hit-test call at
     * 0x2bc40).  It only exits when [0x392e6]!=0 (a selection was made).  Operator froze here
     * after combat.  Once we've sat in it >300 frames without exiting, log the cursor pos +
     * the gate/exit flags + the item-list head, so we can tell a FROZEN cursor (input dead)
     * from a stuck exit-flag (can't select).  Throttled + capped; harmless in normal use. */
    if (g_os && pc == 0x3bdecu) g_inread_n++;       /* count input-routine (mouse read) calls */
    if (g_os && pc == 0x2d624u) g_curupd_n++;       /* count cursor-mover runs (is it still dispatched?) */
    if (g_os && pc == 0x3b91au) g_last_a1 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A1);  /* last dispatched callback */
    if (g_os && pc == 0x2d61cu) {                   /* cursor-mover entry: capture who dispatched/called it */
        g_cursor_cb = g_last_a1;
        g_cur_caller = r32((uint32_t)m68k_get_reg(NULL, M68K_REG_A7));
    }
    if (g_os && pc == 0x2bc40u) {
        static uint32_t ml = 0; static int mn = 0;
        if (r16(0x392e6u)) ml = 0;                 /* selection made -> loop exiting: healthy */
        else if (++ml > 300u && (ml % 120u) == 0u && mn < 80 && g_log) {
            uint32_t lh = r32(0x3a96cu), i0 = 0, i1 = 0;
            if (lh < 0x200000u) { i0 = r32(lh); i1 = r32(lh + 4u); }
            /* inread = is the input routine 0x3bdec still being called? (if frozen across samples,
             * the sound/vblank IRQ stopped reaching it).  raw0/raw1/rbtn = its captured mouse
             * X/Y/button at 0x3c0bc/be/c0 (if those move but curX/curY don't, the cursor-update
             * downstream is dead; if they're frozen too, the capture/IRQ upstream is dead). */
            fprintf(g_log, "MENULOOP ml=%u cur392d4=%d curupd=%u cursorcb=%08x caller=%08x cblist=%08x,%08x,%08x,%08x,%08x,%08x ic=%llu\n",
                    ml, (int16_t)r16(0x392d4u), g_curupd_n, g_cursor_cb, g_cur_caller,
                    r32(0x3c096u), r32(0x3c09au), r32(0x3c09eu), r32(0x3c0a2u), r32(0x3c0a6u), r32(0x3c0aau),
                    (unsigned long long)g_icount);
            fflush(g_log); mn++;
        }
    }
    /* PENALTY-SET watch (2026-06-22): 0x264ce raises the per-knight "lose a life next turn-tick"
     * flag (+0x82) from a scripted overland-event handler.  Capture WHO it flags + the event's
     * type fields + the call path, so a fresh set during CLEAN play is caught at the source
     * (a real hazard vs a stray write) instead of only being seen stuck in a save.  Inert unless
     * the handler actually executes.  (Moved to 0x264b8 = the type-4 body entry, since the block
     * below now redirects there before 0x264ce is reached.) */
    if (g_os && pc == 0x264b8u && g_penaltyset_n < 12 && g_log) {
        uint32_t a1 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A1);  /* knight being flagged */
        uint32_t a0 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A0);  /* event/script object  */
        fprintf(g_log, "PENALTY-SET +0x82=1 knight@%06x lives=%02x  event@%06x [+40]=%04x [+4d]=%02x  ppc=%08x ic=%llu\n",
                a1, (a1 < 0x200000u ? g_ram[a1+0x49u] : 0xffu), a0,
                (a0 < 0x200000u ? r16(a0+0x40u) : 0xffffu), (a0 < 0x200000u ? g_ram[a0+0x4du] : 0xffu),
                (unsigned)m68k_get_reg(NULL, M68K_REG_PPC), (unsigned long long)g_icount);
        fflush(g_log);
        g_penaltyset_n++;
    }
    /* === STOP the day-end life-drain (2026-06-26) ===
     * The +0x82 "lose a life at day-end" flag is a DEAD/BUGGY mechanic: confirmed NOT in the
     * original game -- lives are lost ONLY via combat defeat (CRPG Addict full playthrough + the
     * manual; there is NO day-end / time-limit / starvation life loss, and nothing happens once the
     * black knights are cleared except passing turns).  Operator hit a perpetual per-day drain.
     * Two gameplay-only guards (the attract intro never reaches them, so determinism is unchanged):
     *  (1) skip the WHOLE type-4 penalty body @0x264b8 -- it drains the knight's HIT POINTS at
     *      +0x50 (`sub.w D0,($50,A1)`) AND raises the lose-a-life flag at +0x82.  The entire penalty
     *      is spurious (fires after all the black knights are dead, which the original never
     *      penalizes), so suppress both effects; and
     *  (2) at the day-end dock 0x2171c, CLEAR any already-set flag + SKIP the decrement, so a save
     *      that already has it stuck stops draining at once.  Combat life loss (0x21434/0x2143a)
     *      and the normal day-end turn loop are otherwise untouched. */
    if (g_os && pc == 0x264b8u) {                 /* (1) type-4 penalty body: skip HP-drain + life-flag */
        m68k_set_reg(M68K_REG_PC, 0x264d4u);      /*     -> jump straight to the event's exit jmp        */
        return;
    }
    /* AUD1 stray-disable fix: the sound-start routine @0x4366c blindly clears AUD1's DMA
     * with a hard-coded `move.w #$2,($96,A5)` @0x4371c (the rest of the routine is
     * channel-indexed off A4) -> any one-shot SFX on AUD1 gets chopped when the engine
     * starts a voice on another channel.  Skip the 6-byte stray write (-> the epilogue). */
    if (g_aud1_dmafix && g_os && pc == 0x4371cu) {
        m68k_set_reg(M68K_REG_PC, 0x43722u);
        return;
    }
    /* Canopy-choke off-screen haul: replace the unconditional +150 destX with an inward swing
     * (toward arena centre 160), so a right-side grab hauls the monkey ON-screen instead of past
     * the 320-wide edge.  A0 points at the arc-block destX field (already holding curX from 0x272a4);
     * A1 = the hauled monkey ([0x2ebd0]).  We write the corrected destX and skip the +150 add. */
    if (g_choke_haul_fix && g_os && pc == 0x272a8u) {
        uint32_t a0 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A0);   /* -> arc-block destX (holds curX) */
        uint32_t a1 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A1);   /* the monkey being hauled          */
        int16_t curx = (int16_t)m68k_read_memory_16(a1 + 4u);      /* monkey.X                         */
        int16_t dest = (int16_t)(curx <= 160 ? curx + 150 : curx - 150);  /* swing toward centre       */
        m68k_write_memory_16(a0, (unsigned)(uint16_t)dest);        /* corrected destX                  */
        m68k_set_reg(M68K_REG_A0, a0 + 2u);                        /* mimic the addi.w #,(A0)+ post-inc */
        m68k_set_reg(M68K_REG_PC, 0x272acu);                       /* skip the original +150 add        */
        return;
    }
    if (g_os && pc == 0x2171cu) {                 /* (2) day-end dock reached only when +0x82 set:  */
        uint32_t a0 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A0);
        if (a0 < RAM_SIZE) g_ram[(a0 + 0x82u) & (RAM_SIZE - 1)] = 0;   /* consume the stale flag     */
        m68k_set_reg(M68K_REG_PC, 0x21722u);      /* skip `subi.b #1,($49,A0)` -- no life docked     */
        return;
    }
    /* INV-MENU WALK TRACE (2026-06-26, disassembly-verified): the inventory MENU renderer walks a
     * linked list of entries {+0:name-ptr, +4:x, +6:y, +9:flags, +0xa:next}; A2 = current entry at
     * 0x2a1ae (`movea.l $37444,A2`), then 0x2a1b8 `move.l (A2),..` derefs it.  One entry's +0xa next
     * is corrupt -> A2 goes garbage -> address error.  Ring the entries A2 visits; the instant A2
     * is bad, dump the trail WITH each entry's item-NAME string -> names the leaked moonstone entry
     * + its bad link.  Inert in normal play (valid menus never hit a bad entry).  Read-only. */
    if (g_os && pc == 0x2a1aeu && g_log) {
        static uint32_t ring[32]; static int ri = 0, dumped = 0; static uint32_t lastpush = 0xffffffffu;
        uint32_t e = r32(0x37444);
        int bad = (e >= RAM_SIZE) || (e & 1u) || (e != 0 && e < 0x21000u);
        if (!bad) { if (e != lastpush) { ring[ri % 32] = e; ri++; lastpush = e; } }   /* dedup consecutive repeats */
        else if (!dumped) {
            dumped = 1;
            fprintf(g_log, "=== INV-MENU bad entry=%08x; DISTINCT entry trail (oldest->newest) ic=%llu ===\n",
                    e, (unsigned long long)g_icount);
            int start = ri > 32 ? ri - 32 : 0;
            for (int i = start; i < ri; i++) {
                uint32_t n = ring[i % 32], sp = r32(n);
                char nm[40]; int k = 0;
                if (sp < RAM_SIZE) for (; k < 38; k++) { uint8_t c = g_ram[(sp + k) & (RAM_SIZE - 1)]; if (!c) break; nm[k] = (c >= 32 && c < 127) ? c : '.'; }
                nm[k] = 0;
                fprintf(g_log, "  entry=%06x name@%06x '%s' next+0xa=%08x\n", n, sp, nm, r32(n + 0x0a));
            }
            fflush(g_log);
        }
    }
    if (g_os && !g_blt_busy_scope && g_derail_n < 6) {
        /* PC executing from a region that is NEVER normal code -> LOG-ONLY forensics (do NOT halt).
         * The old blanket "fatal low-RAM HALT" kept false-positiving on legit code that lives all
         * over low RAM -- exec/setup at 0x400 & 0x800, the music driver [0x500,0x800), overlays at
         * 0x75xxx -- and froze boot (00:36) then NEW GAME (0x400, then 0x800).  Removed 2026-06-26.
         * The moonstone-click crash is instead trapped PRECISELY at its deref (0x2a1b8, garbage A2)
         * below, which cannot false-positive on setup code. */
        int code = (pc >= 0x400u && pc < 0x800u) || (pc >= 0x9000u && pc < 0x10000u)
                || (pc >= 0x21000u && pc < 0x68000u) || (pc >= 0x1c0000u && pc < 0x1f0000u);
        if (!code) dump_derail("PC in non-code region (logged)", pc, 0);
    }
    /* TARGETED moonstone-crash trap (2026-06-26): the inventory menu over-walk derefs the current
     * entry at 0x2a1b8 (`move.l (A2),$37440`); if A2 is a garbage pointer (odd or >= RAM) it is
     * about to ADDRESS-ERROR -> vector through the uninitialized vec3 -> runaway that smashes the
     * roster + audio.  HALT + snapshot HERE instead, pre-runaway.  Precise: only a garbage menu
     * entry trips it, so (unlike the old blanket low-RAM trap) it can never freeze legit setup code. */
    if (g_os && pc == 0x2a1b8u) {
        uint32_t a2 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A2);
        if (a2 >= RAM_SIZE || (a2 & 1u)) {
            dump_derail("MOONSTONE-CRASH (garbage menu entry A2)", pc, 1);
            { char p[1280]; snprintf(p, sizeof(p), "%s%sderail.sav", g_exedir[0]?g_exedir:".", g_exedir[0]?"/":"");
              save_state(p);
              if (g_log) { fprintf(g_log, "=== MOONSTONE-CRASH trap: A2=%08x garbage; froze pre-runaway -> derail.sav ic=%llu ===\n",
                                   a2, (unsigned long long)g_icount); fflush(g_log); } }
            halt("moonstone menu over-walk trapped (frozen pre-corruption)");
            return;
        }
    }
    if (g_os && pc >= 0x400000u) {
        /* PC flew into WILD memory (e.g. 0xb0640000 / 0xffffffff from an rts off a smashed A7)
         * -- no code ever lives above chip RAM.  Snapshot once + HALT gracefully here, BEFORE
         * the runaway double-faults or spray-writes the sound code and locks up the window
         * (2026-06-21 host-hang).  Inert in normal play (PC is always < 0x200000 then). */
        static int wn = 0;
        if (wn++ < 4) dump_derail("RUNAWAY wild PC -- halting", pc, 1);
        halt("runaway: PC in wild memory");
        return;
    }
    /* A7-SMASH watch (diagnostic, 2026-06-21): the sound-engine runaway BEGINS when the stack
     * pointer gets corrupted to a wild value (this crash: A7=0xffffffc0) -> the next rts pops a
     * garbage return address -> wild PC.  Log the valid->wild A7 transition + auto-snapshot so
     * the NEXT repro pinpoints the exact instruction that smashes A7 = the actual root.  Inert
     * in normal play (A7 always stays in chip RAM, < 0x400000). */
    if (g_os) {
        static int a7_was_ok = 1, a7n = 0;
        uint32_t a7 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A7);
        /* WIDENED 2026-06-25: the moonstone-click crash smashes A7 to ~0x4351c -- INSIDE the Mog
         * sound-code region [0x43224,0x43bba), below the old 0x400000 "wild" threshold, so it was
         * missed.  Also flag A7 in low RAM (<0x1000): the stack must never live in code or page 0.
         * Logs the ppc = the instruction that set the bad A7 -> names the click->A7 corruptor. */
        int a7ok = (a7 >= 0x1000u) && (a7 < 0x400000u) && !(a7 >= SNDCODE_LO && a7 < SNDCODE_HI);
        if (!a7ok && a7_was_ok && a7n++ < 6) {
            if (g_log) fprintf(g_log, "A7-SMASH a7=%08x at pc=%06x ppc=%08x ic=%llu\n",
                    a7, pc, (unsigned)m68k_get_reg(NULL, M68K_REG_PPC),
                    (unsigned long long)g_icount), fflush(g_log);
            dump_derail("A7-SMASH", pc, 1);
        }
        a7_was_ok = a7ok;
    }
    /* (Removed 2026-06-25 per operator: the per-frame enemy-Moonstone "scrub" band-aid.  It changed
     * game state to mask the symptom instead of fixing the leak; the read-only MS-LEAK-SET (in w8/w16/
     * w32) + A7-SMASH watches remain to find the real root.) */
    /* ===== ROOT FIX (producer side) for the two-troll / double-overhead-swing crash =====
     * 0x3c096..0x3c0ba is the game's PER-VBLANK TASK LIST (the vblank interrupt-server chain;
     * dispatcher 0x3b90a jsr's each entry every frame): cursor-mover, sprite/anim, and timed
     * SCREEN-EFFECT handlers.  0x2a10c is one such effect -- a display-window (DIWSTRT/DIWSTOP)
     * transition animator, NOT sound.  The effect's start routine appends 0x2a10c at the list
     * tail (0x2a0ea) and records that slot in [0x2a15e]; its end removal (0x2a108) clears
     * [[0x2a15e]].  Because [0x2a15e] holds only the LAST-registered slot, two simultaneous
     * starts of the same effect (the double overhead swing's clash) append two 0x2a10c entries
     * but the first is never removed -> it LEAKS.  Leaks accumulate until the 9-slot buffer is
     * full with no NULL terminator, the dispatcher runs past it into the flag/input and jsr's
     * garbage = the crash.  FIX: make registration IDEMPOTENT -- if 0x2a10c is already in the
     * list, skip the duplicate append and point [0x2a15e] at the existing slot so removal still
     * clears the one real entry.  The list then holds at most one 0x2a10c, never overflows, and
     * is removed correctly.  Inert for non-overlapping effects (it's removed between them). */
    /* Opcode guard (hardening 2026-07-02): this 0x2xxxx program region gets OVERLAID by
     * other scene code (see the legend hook below, which checks r16(pc) for the same
     * reason).  Only act when the REAL append instruction (`move.l #$2a10c,(a0)` =
     * 0x20bc) is resident, so an overlay executing through this address is untouched. */
    if (g_os && g_tasklist_fix && pc == 0x2a0eau && r16(pc) == 0x20bcu) {
        for (uint32_t s = 0x3c096u; s < 0x3c0bau; s += 4u) {
            if (r32(s) == 0x2a10cu) {                  /* effect handler already registered -> dedup */
                w32(0x2a15eu, s);                      /* removal target = the existing single slot */
                m68k_set_reg(M68K_REG_PC, 0x2a0f0u);   /* skip the duplicate append (-> the rts) */
                if (g_log && g_taskdedup_n < 20) {
                    fprintf(g_log, "TASKDEDUP 0x2a10c already at %06x -> skip dup append, [0x2a15e]=%06x ic=%llu\n",
                            s, s, (unsigned long long)g_icount);
                    fflush(g_log); g_taskdedup_n++;
                }
                return;
            }
        }
    }
    /* ROOT FIX for the cursor/menu FREEZE.  The same per-vblank task list [0x3c096,0x3c0ba) holds,
     * among the screen-effect tick-handlers (0x2a10c), the menu's CURSOR-UPDATE handler **0x2d61c**
     * (dispatched every vblank by 0x3b90a; it reads the joystick via 0x22fe6 and moves the cursor
     * 0x392d4).  The effect-end REMOVAL at 0x2a108 does `clr.l [[0x2a15e]]`, where 0x2a15e is the
     * SINGLE "last-registered slot".  If the cursor (0x2d61c) was registered AFTER an effect,
     * 0x2a15e points at the CURSOR, so when that effect ends the removal deletes the cursor handler
     * -> it stops being dispatched -> the cursor freezes (confirmed: cursorcb=0x2d61c, gone from
     * cblist).  FIX: the effect-removal must only ever drop an EFFECT handler (0x2a10c).  If 0x2a15e
     * points at a 0x2a10c, remove that (compacting); otherwise it's mis-aimed at the cursor /
     * another task -- find a real 0x2a10c to remove instead and leave 0x2d61c (and friends) alone. */
    if (g_os && g_tasklist_fix && pc == 0x2a108u && r16(pc) == 0x4290u) {  /* opcode-guarded: `clr.l (a0)` resident */
        uint32_t a0 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A0);    /* = [0x2a15e], the intended slot */
        uint32_t slot = a0;
        int misaim = (a0 < 0x3c096u || a0 >= 0x3c0bau || r32(a0) != 0x2a10cu);
        if (misaim) {                                                /* mis-aimed -> pick a real effect slot */
            slot = 0;
            for (uint32_t s = 0x3c096u; s < 0x3c0bau; s += 4u) if (r32(s) == 0x2a10cu) slot = s;
        }
        int shifted = 0;
        if (slot >= 0x3c096u && slot < 0x3c0bau)                    /* compact-remove the chosen 0x2a10c */
            shifted = tasklist_compact(slot);
        /* slot==0 (no 0x2a10c present) -> remove nothing, just skip the clr.l (preserve all). */
        /* Divergence log (hardening 2026-07-02): mis-aimed removal and non-tail compaction
         * are the ONLY cases where this hook's outcome differs from the original clr.l --
         * the 2026-07-02 audit found zero occurrences in all probed flows, so if either
         * ever fires in real play we want the evidence.  Bounded. */
        if ((misaim || shifted) && g_log && g_taskhook_n < 40) {
            fprintf(g_log, "TASKFIX-RM misaim=%d shifted=%d a0=%06x slot=%06x ic=%llu\n",
                    misaim, shifted, a0, slot, (unsigned long long)g_icount);
            fflush(g_log); g_taskhook_n++;
        }
        m68k_set_reg(M68K_REG_PC, 0x2a10au);                        /* skip the original clr.l (-> rts) */
        return;
    }
    /* HARDENING (2026-07-02 audit): the menu-cursor server's UNINSTALL (0x2d5f4) removes
     * its entry with `clr.l (a0)` @0x2d618 where a0 = [0x392da], the slot address saved
     * at INSTALL time (0x2d5de).  If an effect registered BEFORE the cursor completes
     * while the cursor is installed, hook-B compaction shifts the cursor down one slot
     * and the saved address goes stale -> the original would clear the WRONG slot and
     * LEAK the cursor server (dispatched forever; repeat leaks -> 9-slot overflow = the
     * original crash class).  Remove 0x2d61c BY VALUE instead (compacting, hole-free),
     * ignoring the possibly-stale pointer.  In the common case (cursor at the recorded
     * tail slot) this is byte-identical to the original clr.l.  Opcode-guarded. */
    if (g_os && g_tasklist_fix && pc == 0x2d618u && r16(pc) == 0x4290u) {
        uint32_t a0 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A0);    /* = [0x392da] (possibly stale) */
        uint32_t slot = 0;
        for (uint32_t s = 0x3c096u; s < 0x3c0bau; s += 4u) if (r32(s) == 0x2d61cu) slot = s;
        if (slot) {
            tasklist_compact(slot);
            if (slot != a0 && g_log && g_taskhook_n < 40) {         /* the stale-pointer case, now healed */
                fprintf(g_log, "TASKFIX-CURSOR stale [0x392da]=%06x actual=%06x ic=%llu\n",
                        a0, slot, (unsigned long long)g_icount);
                fflush(g_log); g_taskhook_n++;
            }
        }
        /* no 0x2d61c anywhere -> remove nothing (never clr.l a foreign slot) */
        m68k_set_reg(M68K_REG_PC, 0x2d61au);                        /* skip the original clr.l (-> rts) */
        return;
    }
    if (g_os && pc >= 0x9000u && pc < 0x10000u) hle_dispatch(pc);
    /* Arm the sound-engine code-write guard (see SNDCODE_LO note): pc 0x4333c is
     * the Mog sound server's per-voice dispatch (`lea $42fd4,a4` lea-ladder head),
     * which only runs once the engine is fully loaded and ticking -- strictly after
     * the one-time module load.  From here on, any store into the engine's CODE
     * bytes (0x43224..0x43e00) is a wild write and gets dropped+logged. */
    if (!g_snd_loaded && pc == 0x4333cu) g_snd_loaded = 1;
    /* FLOPPY MOTOR/SEEK BUSY-WAIT ACCELERATION (see g_dsk_fastwait note above).
     * Mog's trackdisk driver busy-polls CIA-A ICR ($bfed01) bit0 for a TimerA
     * underflow at two points -- 0x3b7b8 (`btst #0,$bfed01; beq $3b7b8`, the single
     * ~3ms seek delay) and 0x3b8bc (`btst #0,$bfed01; beq $3b8bc`, the 25x ~50ms
     * motor spin-up loop) -- to wait out the physical drive's motor/settle time.
     * For a file-backed load that dead time is pure black-screen.  Assert the
     * TimerA-underflow flag right when the loop is about to poll it so the `beq`
     * falls through after one iteration; the real TimerA keeps running normally
     * (this only pre-sets the latched flag the poll consumes), and the disk DMA /
     * IRQ / seek-step model is untouched.  Inert outside the Mog driver -- these
     * PCs never execute during the attract intro (lightning/audio sync preserved)
     * and only fire while a disk read is in progress. */
    if (g_os && g_dsk_fastwait && (pc == 0x3b7b8u || pc == 0x3b8bcu))
        g_ca.icr_flags |= 0x01;
    /* Multi-candidate numbered popup: its handler busy-waits on the keyboard buffer
     * [0x3bf74] for a rawkey, translating it via the 0xc1b3 table (routine 0x3ff42)
     * to ASCII '1'-'9' to pick the option.  This menu exists TWICE in the image:
     *   - the FRONT-END (`program`) copy at SEED 0x41162 / WAIT 0x41178 -- the overland
     *     overlap popup (zone-vs-city), reached from the map fire-action 0x41134;
     *   - the IN-GAME (`Mog`) copy at SEED 0x1e4756 / WAIT 0x1e476c -- a byte-identical
     *     relocation (delta 0x1a35f4), used by Mog's own numbered selections incl. the
     *     conditional post-combat treasure-TAKE / reward menu.  Both read the SAME
     *     [0x3bf74] buffer + 0xc1b3 table, so neither is controller-selectable without
     *     injection.  Make BOTH joystick/controller/keyboard selectable: at the wait
     *     loop inject the chosen option's rawkey (option i needs rawkey i+1 per 0xc1b3);
     *     Up = option 1, Down = option 2 (the two stacked choices), plus keyboard 1-9.
     *     Seed the edge-state at menu entry so a held navigation direction does NOT
     *     auto-select. */
    if (g_maplog && g_log && g_os && (pc == 0x4114au || pc == 0x1e473eu))  /* candidate count (d0) */
        fprintf(g_log, "CANDCOUNT d0=%u tokenX=%d tokenY=%d\n", (unsigned)m68k_get_reg(NULL,M68K_REG_D0),
                (int)(int16_t)r16(0x2e85a), (int)(int16_t)r16(0x2e85c));
    /* LEGEND TEXT FIX (g_os-gated, legend-scene-only): the attract legend renderer's
     * XOR-mode char-blit skips its own display cookie-cut (see note at g_legend_active),
     * so the story crawl never reaches the screen.  While the legend scene is active,
     * redirect the char-blit's skip point (0x2cda0, the `beq` that bypasses the display
     * copy) into the cookie-cut tail (0x2cda6) so each assembled glyph is blitted onto
     * the display half via the engine's OWN blit -- faithful, no glyph hacking, and
     * fully inert outside the legend (and for the cracktro, g_os=0). */
    /* Arm only for the genuine legend scene: the driver 0x221ca is entered with
     * a0 = the legend record list 0x222c8 (the "druids/Stonehenge/MOONSTONE" crawl).
     * The 0x22xxx region is overlaid by other program scene-handlers, so require the
     * record-list signature in a0 to avoid arming on unrelated code that happens to
     * sit at this address later. */
    if (g_os && pc == 0x221cau && m68k_get_reg(NULL,M68K_REG_A0) == 0x222c8u) g_legend_active = 1;
    /* Begin ramping the attract video delay back to 0 at the legend scene (the final
     * pre-game screen, AFTER all lightning/thunder) so the picture is caught up to live
     * by Mog launch and the -2.5s tail adds no black at the transition (see g_av_ramp). */
    if (g_os && pc == 0x210f6u) g_av_ramp = 1;
    if (g_os && g_legend_active && pc == 0x2221cu) {
        /* Just before the legend text render (jsr 0x28ed2): the driver has already
         * CLEARED the display half [0xbe44]; prime it with the current vortex master
         * (screen-registry slot 0 [0x22462]) so the cookie-cut gold text lands ON the
         * teal vortex rather than a black buffer -- mirrors the original, where the
         * scene background already occupies the displayed half before the overlay. */
        uint32_t disp = r32(0xbe44), slot0 = r32(0x22462);
        if (disp && slot0 && disp < RAM_SIZE && slot0 < RAM_SIZE &&
            (disp + 5u*0x1f40u) < RAM_SIZE && (slot0 + 5u*0x1f40u) < RAM_SIZE) {
            for (uint32_t k = 0; k < 5u*0x1f40u; k++) g_ram[disp + k] = g_ram[slot0 + k];
        }
    }
    if (g_os && (pc == 0x21114u || pc == 0x252f4u)) {
        g_legend_active = 0;        /* Mog launch: scene over */
        g_av_ramp = 0;              /* video-delay ramp finished by now (caught up to live) */
        g_blt_busy_scope = 0;       /* leave the attract scope: stop throttling blits for the
                                     * Mog loader + in-game (see the busy-model note above). */
        /* Drop the attract audio-output delay back to minimal latency for
         * responsive gameplay audio.  Drain the extra backlog once so the ~N-frame
         * intro latency doesn't carry into gameplay (a one-time glitch at the cut is
         * fine).  audio_flush()'s cap now refills to just ~2 frames. */
        if (g_av_attract_audio) {
            g_av_attract_audio = 0;
            if (g_audio_dev) SDL_ClearQueuedAudio(g_audio_dev);
            audio_reprime(4);   /* the cap never refills a cleared queue (it only limits
                                 * pushes) -- rebuild the cushion here too, or gameplay
                                 * runs starved after the attract drain */
        }
        g_av_drain = 1;           /* drain the buffered intro tail over the loader, THEN go live
                                   * (instead of snapping straight to the black load screen) */
    }
    /* (Legend cookie-cut redirect REMOVED: now that the loader-HLE opcode guard above
     * no longer hijacks 0x2cda0 during the char engine, the char engine's OWN `beq.w
     * $2cda6` runs and the per-glyph cookie-cut reaches the display naturally for the
     * WHOLE intro -- legend included -- so the legend-only PC redirect is obsolete.
     * The legend background-prime at 0x2221c above is kept so the text lands on the
     * vortex master.) */
    if (g_os && (pc == 0x41162u || pc == 0x41178u ||      /* program (front-end) copy */
                 pc == 0x1e4756u || pc == 0x1e476cu)) {   /* Mog (in-game) copy */
        int seed = (pc == 0x41162u || pc == 0x1e4756u);
        int cur = g_kdigit ? g_kdigit : (g_ji_up ? 1 : (g_ji_dn ? 2 : 0));
        if (seed) {                           /* menu just appeared: ignore held input */
            g_menusel_prev = cur;
        } else {                              /* waiting on [0x3bf74]: inject on a fresh press */
            if (cur && cur != g_menusel_prev) w16(0x3bf74u, (uint16_t)(cur + 1));
            g_menusel_prev = cur;
        }
    }
    /* TYPED-TEXT ENTRY (Select-Knight custom name): the name-entry FSM busy-polls
     * [0x3bf74] at 0x22be8 (front-end 0x22b84 field) / 0x1c86b8 (byte-identical Mog
     * field 0x1c86d6) -- `tst.w $3bf74; beq <loophead>`.  When the field is waiting
     * (buffer currently empty) and the host has queued a typed key, drop the next
     * 0xc1b3-index into [0x3bf74]; the FSM consumes + clears it (0x22c66/0x1c8736),
     * so exactly one queued press = one character (incl. Return=0x1c confirm and
     * Backspace=0x0e).  Faithful: the injected value lives in the same space the
     * game's own CIA-A keyboard ISR would have produced, and 0x3ff42/0xc1b3 does the
     * ASCII translation.  Inert outside this poll site, so it never touches the
     * digit-menu (separate PCs above), arrows/fire (separate JOY globals), or play. */
    if (g_os && (pc == 0x22b90u || pc == 0x1c8660u))   /* name field just opened: drop stale keys */
        g_keyq_head = g_keyq_tail = 0;
    if (g_os && (pc == 0x22be8u || pc == 0x1c86b8u) && !keyq_empty() && r16(0x3bf74u) == 0)
        w16(0x3bf74u, (uint16_t)keyq_pop());

    /* OPEN INVENTORY on the overland map: the map turn-loop polls [0x3bf74] at 0x4024c,
     * translates it (jsr 0x3ff42), and on ASCII space (0x20) opens the inventory (jsr
     * 0x405b4).  When the operator presses the inventory input (Space / I / pad Y / pad
     * Start -> g_inv_request), inject the Space 0xc1b3-index (0x39, which 0x3ff42 maps to
     * ASCII 0x20) so the poll opens it.  The open-map inventory was otherwise unreachable
     * (Space was wired to fire; no pad button was bound).  Gated to that one poll PC, so
     * it's inert everywhere else (combat/menus/towns use different polls). */
    if (g_os && pc == 0x4024cu) {           /* overland-map keyboard poll */
        g_mappoll_hot = 1;                  /* the normal map turn-loop ran this frame (distinguishes it from the delivery overview) */
        g_in_inventory = 0;                 /* we're back on the map (inventory not open) */
        /* REST / skip-turn: the original maps keyboard 'E' (-> ASCII 0x45 via 0x3ff42) to
         * "end my turn now" -- 0x4028c sets the turn timer [0x2f9dc]=[0x2fa08](max), which the
         * turn-end check at 0x4031a then trips, bumping the player index [0x2f9da].  We inject the
         * 'E' scancode-index (0x12) here just like inventory injects Space (0x39).  g_rest_pending
         * re-clears [0x3bf74] on the very next poll so one keypress = exactly one skipped turn
         * (the 'E' branch, unlike inventory's, never clears the buffer itself). */
        if (g_rest_pending) { w16(0x3bf74u, 0); g_rest_pending = 0; }
        else if (g_inv_request && r16(0x3bf74u) == 0) { w16(0x3bf74u, 0x39); g_inv_request = 0; }
        else if (g_rest_request && r16(0x3bf74u) == 0) { w16(0x3bf74u, 0x12); g_rest_request = 0; g_rest_pending = 1; }
    }
    if (g_os && pc == 0x405b4u) g_in_inventory = 1;   /* inventory screen just opened */

    /* ============================================================================
     * ROOT FIX for the famous original-game "enemy/Guardian HAS a Moonstone" bug
     * (the one whose downstream crash we already contain with sndcode_guard).
     *
     * THE ACTOR/INVENTORY MODEL (RE'd, all in the program/Mog image):
     *   The 4-entry roster is at 0x2e7dc, stride 0x84.  A record's +0x36 = knight
     *   slot: 0-3 = a HUMAN player's chosen knight, 4 = AI enemy, 5 = the Guardian
     *   (the fixed Guardian record is 0x2e9ec, slot 5).  Two fields carry Moonstone
     *   state:  +0x4e = the Moonstone COUNT (word), and the per-actor quest/inventory
     *   struct at [+0x60] holds a byte/word item array whose offset +0x16 is the
     *   Moonstone-TOKEN bitfield (item id 0x16) and +0x14 = the four-keys byte (item
     *   id 0x14).  New-game init (0x2608e) clears every actor's +0x4e=0 and zeroes
     *   the whole +0x60 struct, so enemies START with no Moonstone.
     *
     * HOW A MOONSTONE LEAKS INTO A NON-PLAYER (the bug, two paths, both in combat
     * win-resolution -- proven by disassembly; these are the ONLY writers of +0x4e
     * or of item 0x16 into a record):
     *   (1) +0x4e COUNT, AI combat winner.  At 0x21b90 the winner record is in a0;
     *       `cmpi.l #4,$36(a0); beq $21bf2` -- i.e. ONLY when the winner is an AI
     *       enemy (slot 4) does it fall to 0x21bf2 `addi.w #1,$4e(a0)` (the enemy
     *       gets a Moonstone).  When the winner is a PLAYER (slot 0-3) it instead
     *       goes to 0x21b9c = the interactive loot/reward screen (scene 1) -- a
     *       SEPARATE path that never touches 0x21bf2.
     *   (2) TOKEN (item 0x16), inherited on a death-transfer.  The item-transfer
     *       routine 0x2150e (a0=winner, a1=loser) moves the loser's items into the
     *       winner.  Items 0x16 (token) and 0x14 (keys) are OR-merged into the
     *       winner's +0x60 struct at exactly two sites: 0x215ca (routine B, loser
     *       permanently dead -- full transfer) and 0x215ec (routine A, loser still
     *       alive -- "steal one item").  Callers whose winner can be a NON-player:
     *       0x21bf8 (AI won the wilderness/actor fight) and 0x2221c (the PLAYER LOST
     *       to the Guardian -> the Guardian record 0x2e9ec inherits the token).
     *
     * WHY THIS CANNOT TOUCH THE LEGIT WIN PATH:
     *   The player-Moonstone awards are entirely independent of the above:
     *     - Guardian DEFEATED -> player +3 : 0x22738 `addi.w #3,$4e(a0)` with
     *       a0=[0x2ebd0] (the active player), no 0x2150e involved.
     *     - "season of Moonstones" altar -> player +1 : 0x21cfa, a1=[0x2ebd0].
     *     - special encounter win -> player +2 : 0x22240, a0=[0x2e1f6] (the player).
     *     - grave pillage : 0x40e58 calls 0x2150e with a0=[0x2ebd0] (player=winner).
     *     - player wins a fight : the loot screen (0x21b9c, scene 1), where the
     *       player takes items via the click dispatcher (edits [0x2fb08]=player rec).
     *   Every one of those has a PLAYER as the destination, so the guards below --
     *   which fire ONLY when the destination/winner record's slot is 4 (AI) or 5
     *   (Guardian) -- are provably inert on the win path.  A player who beats a
     *   token-carrying enemy still legitimately inherits the token (winner slot 0-3,
     *   guard does not fire).
     *
     * THE SURGICAL FIX (faithful, Moonstone-only, g_os-gated):
     *   (1) at 0x21bf2, redirect PC to 0x21bf8 -- skip the AI winner's +0x4e
     *       increment (we only reach 0x21bf2 when the winner is AI, so no test
     *       needed).  The AI still loots its normal items via the 0x2150e call.
     *   (2) at the token-merge entry points 0x215c2 / 0x215e4, if the current item
     *       d0 == 0x16 (Moonstone-token) AND the winner (a0) is a non-player
     *       (slot 4 or 5), redirect PC to that block's normal exit (routine B ->
     *       0x21596 next-item; routine A -> 0x21578 done) so the token's
     *       read/OR/write/clr is skipped ENTIRELY -- the token stays where it was
     *       and is never written into the enemy/Guardian.  d0 != 0x16 (keys 0x14,
     *       and the additive items handled elsewhere) and player winners are
     *       untouched, so enemies still hold/drop all their normal loot.
     * ============================================================================ */
    if (g_os && pc == 0x21bf2u) {              /* AI combat winner: skip the +0x4e Moonstone count */
        m68k_set_reg(M68K_REG_PC, 0x21bf8u);   /* jump past `addi.w #1,$4e(a0)` to the item-transfer call */
        g_msleak_blocked++;
        if (g_maplog && g_log)
            fprintf(g_log, "MS-LEAK-BLOCK fr=%d ic=%llu enemy +0x4e Moonstone-count award SUPPRESSED (winner=%06x slot=%d)\n",
                    g_cur_frame, (unsigned long long)g_icount,
                    (unsigned)m68k_get_reg(NULL,M68K_REG_A0),
                    (int)r32(m68k_get_reg(NULL,M68K_REG_A0)+0x36));
        return;
    }
    /* LOOT-EVENT trace (always-on, 2026-06-26): the moonstone leaks during COMBAT loot, not on the
     * menu click -- log the item-transfer entry (0x2150e: a0=winner, a1=loser) so any enemy looting
     * a defeated carrier is captured with both records' slots.  Bounded.  Read-only. */
    if (g_os && pc == 0x2150eu && g_log) {
        static int le = 0;
        if (le < 60) { le++;
            uint32_t w = m68k_get_reg(NULL,M68K_REG_A0), l = m68k_get_reg(NULL,M68K_REG_A1);
            fprintf(g_log, "LOOT-XFER winner=%06x slot=%d  loser=%06x slot=%d  ic=%llu\n",
                    w, (w<RAM_SIZE)?(int)r32(w+0x36):-1, l, (l<RAM_SIZE)?(int)r32(l+0x36):-1,
                    (unsigned long long)g_icount);
            fflush(g_log);
        }
    }
    if (g_os && (pc == 0x215c2u || pc == 0x215e4u)) {   /* token/keys merge into the winner's +0x60 struct */
        uint32_t d0  = m68k_get_reg(NULL,M68K_REG_D0) & 0xffff;
        uint32_t win = m68k_get_reg(NULL,M68K_REG_A0); /* a0 = winner record (preserved through 0x2150e) */
        uint32_t slot = (win < RAM_SIZE) ? r32(win+0x36) : 0;
        /* ALWAYS-ON quest-token loot trace: every 0x14/0x16 token transfer, with winner slot, so a
         * leak is captured with the exact item id even when the block below doesn't fire. Bounded. */
        { static int lt = 0; if (g_log && lt < 80) { lt++;
            fprintf(g_log, "LOOT-TOKEN pc=%06x item=%04x winner=%06x slot=%d ic=%llu%s\n",
                    pc, d0, win, (int)slot, (unsigned long long)g_icount,
                    (d0==0x16u && (slot==4u||slot==5u)) ? " -> BLOCKED" : ""); fflush(g_log); } }
        /* BROADENED 2026-06-26: block the moonstone token (item 0x16 -- a BITFIELD holding all 4
         * moonstone phases) from transferring to ANY non-player winner (slot NOT 0-3), not just
         * slots 4/5.  Operator: enemies must never loot ANY moonstone; the original game let them
         * (a never-fixed dev bug).  Slot is the 32-bit type field the game itself tests with
         * `cmpi.l #4,$36(a0)` (0-3 player, 4 AI, 5 Guardian).  Item 0x14 = "Sword of Sharpness"
         * (a unique weapon, NOT a moonstone) -- deliberately NOT blocked; this is moonstone-only.
         * EXCLUDE slot 5 (Guardian): it LEGITIMATELY holds the moonstone (the random 1-of-4 reward
         * the player takes when beating it), so it must be allowed to win/hold one.  Block only
         * true enemies = any non-player winner that is NOT the Guardian. */
        if (d0 == 0x16u && slot != 0u && slot != 1u && slot != 2u && slot != 3u && slot != 5u) {
            m68k_set_reg(M68K_REG_PC, (pc == 0x215c2u) ? 0x21596u   /* routine B: continue to next item */
                                                        : 0x21578u);/* routine A: finish (its normal token exit) */
            g_msleak_blocked++;
            if (g_log)
                fprintf(g_log, "MS-LEAK-BLOCK fr=%d ic=%llu quest-token=%04x transfer to non-player SUPPRESSED (winner=%06x slot=%d)\n",
                        g_cur_frame, (unsigned long long)g_icount, d0, win, (int)slot), fflush(g_log);
            return;
        }
    }

    /* nb floppy-loader HLE: intercept LOAD-BY-NAME / STREAM-READ / STREAM-SKIP, run
     * them in C, then RTS — the original disk/OFS bodies never execute.
     *
     * CRITICAL OVERLAY GUARD (fixes the still-intro bug): nb's resident loader hunks
     * live in seg9 (0x2ca08..0x2d290).  Once boot finishes, `program`'s ATTRACT
     * front-end OVERLAYS that same region with nb's RESIDENT CHAR/OBJECT ENGINE
     * (the XOR text+bob blitter that draws the intro's walking monks, the platform
     * ritual, the moon/forest CREDITS, and the legend crawl).  The stream-read
     * routine's address 0x2cda0 collides EXACTLY with the char engine's per-glyph
     * display cookie-cut branch (`beq.w $2cda6`): with the unconditional trap below,
     * every glyph/bob the engine tries to blit onto the displayed half (0x6bdfa) was
     * hijacked into a dead "stream-read" + RTS, so ALL animated intro content was
     * discarded and only the pre-composited still backgrounds rendered.  GUARD: fire
     * a loader trap only when the live opcode at that PC is still the loader routine's
     * signature (load-by-name=0x23c8, stream-read=0x4279, stream-skip=0x33fc; the
     * relocated `program` copies share these first opcode words).  When the char
     * engine (or any other code) has been overlaid there, the opcode differs and the
     * real instruction is allowed to execute — so the cookie-cut runs and the intro
     * animates.  Inert for the cracktro (g_os=0). */
    if (g_os) {
        if ((pc == NB_LOAD_BY_NAME || pc == PR_LOAD_BY_NAME) && r16(pc) == 0x23c8u) { hle_load_by_name(); return; }
        if ((pc == NB_STREAM_READ  || pc == PR_STREAM_READ)  && r16(pc) == 0x4279u) { hle_stream_read();  return; }
        if ((pc == NB_STREAM_SKIP  || pc == PR_STREAM_SKIP)  && r16(pc) == 0x33fcu) { hle_stream_skip();  return; }
        /* SEAMLESS DISK SWAP: the prompt-display routine @0x23fea is entered with
         * a0 = a prompt descriptor; [a0+2] = low word of the "Please insert Disk X"
         * string address (0x024f=A, 0x023a=B, 0x0264=C).  When this is a disk-swap
         * prompt, instantly insert the requested ADF + arm an auto-fire so the
         * "Press fire when done" wait (0x22fd0) passes through invisibly.  No-op
         * once the player drives swaps manually with --diskat. */
        if (g_autoswap && pc == 0x23feau) {
            uint32_t desc = m68k_get_reg(NULL, M68K_REG_A0);
            uint16_t slo  = r16(desc + 2);          /* low word of the prompt string addr */
            int want = -1;
            if      (slo == 0x024f) want = 0;       /* Disk A */
            else if (slo == 0x023a) want = 1;       /* Disk B */
            else if (slo == 0x0264) want = 2;       /* Disk C */
            if (want >= 0 && want < 3 && g_adf[want]) {
                if (g_disk_inserted != want) {
                    g_disk_inserted = want;
                    g_chng_low = 1;                 /* /CHNG: signal the disk changed */
                    g_autoswaps++;
                    if (g_log) fprintf(g_log,
                        "AUTO-SWAP -> Disk%c (ADF idx %d) at the \"insert disk\" prompt (ic=%llu)\n",
                        'A'+want, want, (unsigned long long)g_icount);
                }
                /* Arm the auto-confirm, but let the new disk settle for a few
                 * frames first (the game must observe /CHNG and re-read the
                 * disk's directory to validate it BEFORE we press fire -- pressing
                 * immediately makes the validation fail and it re-prompts).
                 * During a moonstone DELIVERY, hold the "Please insert Disk A" prompt
                 * on screen ~3s (full length) instead of the seamless ~0.25s flash,
                 * because the operator wants that prompt shown as a normal scene there;
                 * normal-play swaps stay seamless (12). */
                if (!g_autoswap_armed) g_autoswap_settle = (g_delivery_win > 0) ? 150 : 12;
                g_autoswap_armed = 1;
            }
        }
        /* SEAMLESS DISK SWAP, part 2: synthesise the "Press fire when done"
         * confirmation, but ONLY while the game is actually sitting in the fire
         * wait at 0x22fd0 (the press loop: bsr 0x22fe6 / btst #4,d1 / beq 0x22fd0)
         * -- and after the settle window so the disk has been validated.  Drive
         * the port-1 fire (/FIR1, g_fire2) HIGH at the press-loop body and release
         * it at the release loop (0x22fda) so both halves of the wait fall
         * through.  PC-gated so it can't race ahead of the wait. */
        if (g_autoswap && g_autoswap_armed && g_autoswap_settle == 0) {
            if (pc == 0x22fd0u) g_fire2 = 1;             /* press loop: assert fire */
            else if (pc == 0x22fdau) {                   /* release loop reached: done */
                g_fire2 = 0; g_autoswap_armed = 0;
                if (g_log) fprintf(g_log, "AUTO-SWAP confirmed (synthetic fire) ic=%llu\n",
                                   (unsigned long long)g_icount);
            }
        }
        /* program's entry (0x21000, reached AFTER nb streamed `program`) stores its
         * two work-memory pools from the startup regs: A1/D1 = graphics pool
         * (base/size), A0/D0 = data pool.  nb's native allocator computed pool bases
         * (chip ~0xbf90, data ~0x3001c) that OVERLAP program's own loaded image
         * (0x21000..0x67210) -- because the harness places program's code at a fixed
         * low base inside what nb's AmigaDOS-style allocator regards as free chip RAM.
         * The screen decoder then overruns program's code/globals -> wild pointer ->
         * unmapped halt at 0x2a9f8.  Re-base both pools above program's image, into
         * otherwise-unused RAM, so decodes can't trample code.  (g_os-gated; the
         * cracktro never loads `program` so g_program_served stays 0.)
         *
         * NOTE: the front-end's launch path runs THIS entry (0x21000) MORE THAN
         * ONCE -- once at the initial `program` boot, and AGAIN when the title
         * screen launches the Mog gameplay engine (the 0x75xxx decruncher
         * recomputes the same low-memory pools at A1=gfx-base/A0=data-base and
         * re-`jmp`s to 0x21000).  The 2nd launch's natively-computed pools
         * (gfx ~[0xca84,0x6b000), data ~[0x454c8,memtop)) OVERLAP the code image
         * (0x21000..0x67210) -- and Mog's buffer table at 0x211b6 carves the
         * knight/sprite buffers (kn1-5.ob, He1-3.ob, blo.cel) out of the gfx
         * pool, so kn1.ob's ~160KB decompress at 0x1c09c overran the code and
         * crashed (ILLEGAL @0x500).  Re-base on EVERY entry (not one-shot) so the
         * Mog relaunch also lands its pools in free RAM above the code. */
        if (pc == 0x21000u && g_program_served) {
            /* The gfx pool must clear TWO fixed regions, not just the code image:
             *   (a) program/Mog's loaded code at 0x21000..0x67210, and
             *   (b) the DISPLAY DOUBLE-BUFFER, which `program`/`Mog` keep at FIXED
             *       baked-in addresses 0x6bdfa and 0x75a3c (each a 5-plane 320x200
             *       screen of 0x9c40 bytes), spanning 0x6bdfa..0x7f67c.  These
             *       buffer bases are constants in the image (copied into the display
             *       pointer pair [0xc0dc]/[0xc0e0]), NOT derived from the pool base.
             * The gfx pool's FIRST allocation is the per-frame WORK/COMPOSE buffer
             * (the background is decompressed into it, then blit-copied to the
             * display buffer each frame -- routine at 0x2a038, src = pool base).
             * If the gfx pool starts at the old 0x68000, that work buffer
             * (0x68000..0x71c40) physically OVERLAPS the display buffer at 0x6bdfa,
             * so the compose blit copies its own source on top of itself and
             * planes 2-4 come out as noise (the systemic upper-plane corruption).
             * Fix: base the gfx pool ABOVE the fixed display buffers (>= 0x7f67c).
             * Layout (all clear of code, the display buffers, and each other; within
             * 2MB; gfx needs ~0x5bf18, data ~0x5654d per the 0x211bc/0x21258 bumps):
             *   gfx  pool: 0x80000 .. 0xe0000  (0x60000)
             *   data pool: 0xe0000 .. 0x1e0000 (0x100000)
             */
            m68k_set_reg(M68K_REG_A1, 0x00080000u); /* graphics pool base (above display bufs) */
            m68k_set_reg(M68K_REG_D1, 0x00060000u); /* graphics pool size  */
            m68k_set_reg(M68K_REG_A0, 0x000e0000u); /* data pool base      */
            m68k_set_reg(M68K_REG_D0, 0x00100000u); /* data pool size      */
            if (g_log && !g_pools_relocated) fprintf(g_log,
                "POOL-RELOCATE @program-entry: gfx[0x80000,+0x60000) data[0xe0000,+0x100000) (above fixed display bufs 0x6bdfa/0x75a3c)\n");
            g_pools_relocated++;
        }
    }
    /* --poke (DEBUG-ONLY, g_os-gated): apply each requested memory write ONCE
     * when PC first reaches its gate address.  Used purely to fast-forward to
     * end-game state for verification (e.g. set the Moonstone count, the Valley
     * key gate, or the map position) without a long real-time playthrough.
     * Inert unless --poke is given (g_npokes stays 0). */
    if (g_npokes && g_os) {
        for (int i = 0; i < g_npokes; i++) {
            Poke *pk = &g_pokes[i];
            if (pk->applied || pc != pk->at_pc) continue;
            if      (pk->size == 1) w8 (pk->addr, (uint8_t) pk->val);
            else if (pk->size == 4) w32(pk->addr,            pk->val);
            else                    w16(pk->addr, (uint16_t)pk->val);
            pk->applied = 1;
            if (g_log) fprintf(g_log,
                "POKE @%06x <= %0*x (size %d) at pc=%06x ic=%llu\n",
                pk->addr, pk->size*2, pk->val, pk->size, pc,
                (unsigned long long)g_icount);
        }
    }
    /* `--dumpat ICOUNT FILE`: snapshot the full 2MB RAM image at a target
     * instruction count then halt (debug; inert unless g_dumpat set). */
    if (g_dumpat && g_icount >= g_dumpat) {
        FILE *rf=fopen(g_dumpat_path?g_dumpat_path:"build/ram_at.bin","wb");
        if(rf){fwrite(g_ram,1,RAM_SIZE,rf);fclose(rf);}
        if (g_log) {
            uint16_t *r=g_custom;
            fprintf(g_log, "DUMPAT-REGS dmacon=%04x bplcon0=%04x bplcon1=%04x diwstrt=%04x diwstop=%04x ddfstrt=%04x ddfstop=%04x cop1lc=%06x bpl0pt=%06x bpl1pt=%06x intena=%04x\n",
                r[0x096>>1], r[0x100>>1], r[0x102>>1], r[0x08e>>1], r[0x090>>1], r[0x092>>1], r[0x094>>1],
                ((uint32_t)r[0x080>>1]<<16)|r[0x082>>1],
                ((uint32_t)r[0x0e0>>1]<<16)|r[0x0e2>>1],
                ((uint32_t)r[0x0e4>>1]<<16)|r[0x0e6>>1], r[0x09a>>1]);
            for (int i=0;i<32;i++) fprintf(g_log,"  color%d=%03x\n", i, r[(0x180+i*2)>>1]&0xfff);
        }
        halt("dumpat reached");
    }
    /* JUMP-TO-LOW = control transfer into the exception-vector table / very low
     * scratch (< 0x400).  This is the stack-corruption signature (an `rts` that
     * popped a 0 / garbage return address).  NOTE: the running game legitimately
     * installs interrupt handlers at 0x400.. (audio/copper server at 0x440, its
     * dispatch trampoline at 0x400, sub-routines up to ~0x540) and EXECUTES them
     * via `jsr $400`/`jsr (a1)` from its VBlank ISR -- those are NOT bugs, so the
     * threshold is 0x400, below the installed-handler region. */
    if (pc < 0x400u && g_prev_pc >= 0x1000u && g_log) {
        char dis[256]; m68k_disassemble(dis, g_prev_pc, M68K_CPU_TYPE_68000);
        fprintf(g_log, "*** JUMP-TO-LOW pc=%06x from prev=%06x (%s) a7=%06x sr=%04x\n",
                pc, g_prev_pc, dis, (unsigned)m68k_get_reg(NULL,M68K_REG_SP), (unsigned)m68k_get_reg(NULL,M68K_REG_SR));
    }
    if (g_l3trace && pc == g_l3trace && g_log && g_l3trace_n < 60000) {
        /* At IRQ-handler entry, A7 points just below the pushed exception frame.
         * Log it every time to confirm the supervisor stack returns to the same
         * value each frame (no monotonic drift over thousands of frames). */
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
        fprintf(g_log, "L3ENTRY #%u ic=%llu a7=%06x sr=%04x frame_sr=%04x pc_ret=%06x\n",
                g_l3trace_n, (unsigned long long)g_icount, sp,
                (unsigned)m68k_get_reg(NULL,M68K_REG_SR),
                r16(sp), r32(sp+2));
        g_l3trace_n++;
    }
    /* --maplog: instrument the overland-map node-entry path so we can see why
     * (or whether) firing on a knight-start node launches a scene.  All reads,
     * gated on g_maplog, zero-cost when off. */
    if (g_maplog && g_log && g_os) {
        /* attract-FSM intro markers (gated on --maplog, inert otherwise): the
         * scripted attract orchestrator @0x210ba shows each story screen then
         * 0x210f6=legend-text scene, 0x21114=launch Mog. */
        if (pc == 0x210f6u) fprintf(g_log,"INTRO-LEGEND fr=%d ic=%llu jsr 221ca a0=%06x (legend text scene)\n",g_cur_frame,(unsigned long long)g_icount,(unsigned)m68k_get_reg(NULL,M68K_REG_A0));
        if (pc == 0x21114u) fprintf(g_log,"INTRO-MOG-LAUNCH fr=%d ic=%llu jmp 252f4 (Mog engine)\n",g_cur_frame,(unsigned long long)g_icount);
        /* numbered-menu wait loops (program + Mog copies): log the injected rawkey so a
         * controller selection is observable.  Inert without --maplog. */
        if (pc == 0x41178u) fprintf(g_log,"MENU-WAIT-PROG ic=%llu (overlap popup 0x41178) inj=%04x\n",(unsigned long long)g_icount,(unsigned)r16(0x3bf74));
        if (pc == 0x1e476cu) fprintf(g_log,"MENU-WAIT-MOG ic=%llu (in-game digit menu 0x1e476c) inj=%04x\n",(unsigned long long)g_icount,(unsigned)r16(0x3bf74));
        if (pc == 0x40204u) {                      /* about to read map input into d1 -> [0x2f9de] */
            uint32_t p = r32(0x2ebd0);
            fprintf(g_log, "MAP-IN fr=%d ic=%llu token=(%d,%d) k36=%u dir[2f9de]=%04x scene=%u\n",
                    g_cur_frame, (unsigned long long)g_icount,
                    (int16_t)r16(p+0x7e), (int16_t)r16(p+0x80),
                    r32(p+0x36), r16(0x2f9de), r32(0x2fb1c));
        }
        if (pc == 0x4020au) {                      /* d1 just loaded from 0x22fe6 (input dir) */
            fprintf(g_log, "MAP-DIR ic=%llu d1=%04x (joy/port1 dir+fire)\n",
                    (unsigned long long)g_icount,
                    (unsigned)m68k_get_reg(NULL,M68K_REG_D1));
        }
        if (pc == 0x40102u) {                      /* about to jsr 0x21e60 node-walk */
            uint32_t p = r32(0x2ebd0);
            fprintf(g_log, "MAP-WALK fr=%d ic=%llu token=(%d,%d) e1=(%d,%d) e2=(%d,%d) e3=(%d,%d)\n",
                    g_cur_frame, (unsigned long long)g_icount,
                    (int16_t)r16(p+0x7e), (int16_t)r16(p+0x80),
                    (int16_t)r16(0x2e860+0x7e), (int16_t)r16(0x2e860+0x80),
                    (int16_t)r16(0x2e8e4+0x7e), (int16_t)r16(0x2e8e4+0x80),
                    (int16_t)r16(0x2e968+0x7e), (int16_t)r16(0x2e968+0x80));
        }
        if (pc == 0x4113au) {                      /* enter fire-action 0x41134: candidate list at 0x30138 */
            int n=0; for(int i=0;i<5;i++) if(r32(0x30138+i*8)) n++;
            fprintf(g_log, "MAP-FIRE ic=%llu candlist_nonempty=%d ent0=%08x/%08x ent1=%08x/%08x\n",
                    (unsigned long long)g_icount, n,
                    r32(0x30138), r32(0x3013c), r32(0x30140), r32(0x30144));
        }
        if (pc == 0x21ab4u) {                      /* combat-vs-actor launch (code 0x21/1) */
            fprintf(g_log, "MAP-ENTER-NODE ic=%llu a0=%06x a1=%06x  *** 0x21ab4 actor-encounter ***\n",
                    (unsigned long long)g_icount,
                    (unsigned)m68k_get_reg(NULL,M68K_REG_A0),
                    (unsigned)m68k_get_reg(NULL,M68K_REG_A1));
        }
        if (pc == 0x2173cu) {                      /* combat scene setup/load (wilderness fight) */
            fprintf(g_log, "MAP-COMBAT-LOAD ic=%llu fr=%d *** 0x2173c combat-scene setup ***\n",
                    (unsigned long long)g_icount, g_cur_frame);
        }
        if (pc == 0x23feau) {                      /* disk-insert prompt: [a0+2]=disk-name str low word */
            uint32_t a0 = m68k_get_reg(NULL,M68K_REG_A0);
            fprintf(g_log, "MAP-DISKPROMPT ic=%llu fr=%d a0=%06x namelow=%04x (024f=A 023a=B 0264=C)\n",
                    (unsigned long long)g_icount, g_cur_frame, a0, r16(a0+2));
        }
        if (pc == 0x220a6u) {                      /* node-action dispatcher (any node id) */
            fprintf(g_log, "MAP-NODE-ACTION ic=%llu fr=%d d0=node?  *** node action dispatched ***\n",
                    (unsigned long long)g_icount, g_cur_frame);
        }
        if (pc == 0x220dau) {                      /* d0 = node id at the cmp ladder */
            fprintf(g_log, "MAP-NODE-ID ic=%llu node_id=0x%02x\n",
                    (unsigned long long)g_icount,
                    (unsigned)(m68k_get_reg(NULL,M68K_REG_D0)&0xffff));
        }
        if (pc == 0x2bc0au) {                      /* scene-type dispatch: d0 -> [0x2fb1c] */
            fprintf(g_log, "MAP-SCENE ic=%llu fr=%d new-scene-type d0=%u\n",
                    (unsigned long long)g_icount, g_cur_frame,
                    (unsigned)m68k_get_reg(NULL,M68K_REG_D0));
        }
        if (pc == 0x2bc40u) {                      /* equip-screen loop hit-test (cursor) */
            static int last=-1;
            if (g_cur_frame/30 != last) {          /* throttle: every ~30 frames */
                last = g_cur_frame/30;
                fprintf(g_log, "EQUIP fr=%d ic=%llu cursor=(%d,%d) scene=%u exitflag=%u\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (int16_t)r16(0x392d4), (int16_t)r16(0x392d6),
                        r32(0x2fb1c), r16(0x392e6));
            }
        }
        if (pc == 0x2cd7eu) {                      /* equip click action (Exit etc.) */
            fprintf(g_log, "EQUIP-CLICK fr=%d ic=%llu cursor=(%d,%d)\n",
                    g_cur_frame, (unsigned long long)g_icount,
                    (int16_t)r16(0x392d4), (int16_t)r16(0x392d6));
        }
        /* TOWN/SERVICE choice screen (node 0x19 -> 0x223ee). The menu loop @0x2243e
         * reads cursor [0x392d4]/[0x392d6], hit-tests via 0x2a502, and on fire
         * dispatches by the matched hotspot's action code [a0+0x10] (1=Merchant,
         * 2=Tavern, 3=Healer, 4=High Temple, 5=Exit). */
        if (pc == 0x2243eu) {                      /* town menu loop: about to read cursor */
            static int last=-1;
            if (g_cur_frame/20 != last) {          /* throttle: every ~20 frames */
                last = g_cur_frame/20;
                fprintf(g_log, "TOWN fr=%d ic=%llu cursor=(%d,%d)\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (int16_t)r16(0x392d4), (int16_t)r16(0x392d6));
            }
        }
        if (pc == 0x22466u) {                      /* town: fire registered over a hotspot */
            uint32_t a0 = m68k_get_reg(NULL,M68K_REG_A0);
            fprintf(g_log, "TOWN-SELECT fr=%d ic=%llu cursor=(%d,%d) action=%u "
                    "(1=Merchant 2=Tavern 3=Healer 4=HighTemple 5=Exit)\n",
                    g_cur_frame, (unsigned long long)g_icount,
                    (int16_t)r16(0x392d4), (int16_t)r16(0x392d6),
                    (unsigned)r32(a0+0x10));
        }
        /* SERVICE-CLICK: the item click dispatcher @0x2cda6 has just resolved the
         * matched hotspot.  a2=hotspot, a3=[a2+8]=label, d1=[a2+0x16]=arg(item-id),
         * d2=[a2+0x14]=type.  Log the click + the player char-record currency
         * [+0x4a] and equip state so a buy/sell transaction is observable. */
        if (pc == 0x2cda6u) {
            uint32_t rec = r32(0x2fb08);
            fprintf(g_log, "SVC-CLICK fr=%d ic=%llu scene=%u cursor=(%d,%d) "
                    "type=0x%02x arg=0x%02x | rec=%06x gold4a=%d wpn5c=%u arm58=%u hit50=%d\n",
                    g_cur_frame, (unsigned long long)g_icount, r32(0x2fb1c),
                    (int16_t)r16(0x392d4), (int16_t)r16(0x392d6),
                    (unsigned)r16(m68k_get_reg(NULL,M68K_REG_A0)+0x14),
                    (unsigned)r16(m68k_get_reg(NULL,M68K_REG_A0)+0x16),
                    rec, (int16_t)r16(rec+0x4a), r32(rec+0x5c),
                    r32(rec+0x58), (int16_t)r16(rec+0x50));
        }
        /* ---- END-GAME / WIN-PATH instrumentation (all read-only logging) ----
         * The Moonstone count lives at player_rec+0x4e ([0x2ebd0]+0x4e); the
         * Valley-of-the-Gods node (id 0x1c, map 152,97) gates on the four-keys
         * byte [player+0x60 +0x14]==0xf and, on a Guardian win, awards +3 ([+0x4e]);
         * the code-2 "season of the Moonstones" altar (0x21ca4) awards +1; the
         * quest is COMPLETED when the player returns the matching Moonstone-token
         * (player+0x60 +0x16 bit) to its home village (node 0x1b), which shows
         * "You have completed the quest" (0x22876) and relaunches the front-end
         * with the win-code in $3e0 (0x2289e). */
        {
            uint32_t pr = r32(0x2ebd0);
            if (pc == 0x226e6u)                        /* Valley node: 4-keys gate passed -> Guardian battle */
                fprintf(g_log, "VALLEY-GUARDIAN fr=%d ic=%llu keys[+14]=0x%02x moonstones[+4e]=%d -> Guardian battle\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (unsigned)r8(r32(pr+0x60)+0x14), (int16_t)r16(pr+0x4e));
            else if (pc == 0x226d2u)                   /* Valley node: gate FAILED ("need all four keys") */
                fprintf(g_log, "VALLEY-LOCKED fr=%d ic=%llu keys[+14]=0x%02x (need 0xf)\n",
                        g_cur_frame, (unsigned long long)g_icount, (unsigned)r8(r32(pr+0x60)+0x14));
            else if (pc == 0x22738u)                   /* Guardian defeated: +3 Moonstones */
                fprintf(g_log, "GUARDIAN-WIN fr=%d ic=%llu moonstones %d -> %d (+3)\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (int16_t)r16(pr+0x4e), (int16_t)r16(pr+0x4e)+3);
            else if (pc == 0x21cfau)                   /* altar "season of Moonstones": +1 Moonstone */
                fprintf(g_log, "ALTAR-MOONSTONE fr=%d ic=%llu moonstones %d -> %d (+1)\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (int16_t)r16(pr+0x4e), (int16_t)r16(pr+0x4e)+1);
            else if (pc == 0x21bf2u)                   /* wilderness fight won: +1 Moonstone */
                fprintf(g_log, "COMBAT-MOONSTONE fr=%d ic=%llu moonstones %d -> %d (+1)\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (int16_t)r16(pr+0x4e), (int16_t)r16(pr+0x4e)+1);
            else if (pc == 0x22876u)                   /* QUEST COMPLETE: "You have completed the quest" */
                fprintf(g_log, "QUEST-COMPLETE fr=%d ic=%llu home[+12]=0x%02x tokens[+16]=0x%02x moonstones=%d *** ENDING ***\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (unsigned)r16(0x2e0bc+0x12), (unsigned)r8(r32(pr+0x60)+0x16),
                        (int16_t)r16(pr+0x4e));
            else if (pc == 0x2289eu)                   /* win-code -> $3e0, then relaunch front-end */
                fprintf(g_log, "QUEST-RELAUNCH fr=%d ic=%llu win-code d7=0x%04x -> $3e0, relaunch 'program'\n",
                        g_cur_frame, (unsigned long long)g_icount,
                        (unsigned)(m68k_get_reg(NULL,M68K_REG_D7)&0xffff));
        }
        /* ---- MULTI-PLAYER / TURN-HANDOFF / DEATH-LIVES instrumentation (read-only) ----
         * The roster is the 4-entry actor array at 0x2e7dc (stride 0x84); a record's
         * +0x36 = knight slot (0-3 = a human player's chosen knight, 4 = AI enemy,
         * 5 = the Guardian).  The number of human knights = [0x2e024] (the "Players"
         * menu value, 1-4).  The overland day is a round-robin: [0x2f9da] is the
         * active-actor rotation index (0-3, mod 4); the TURN-SELECT routine 0x403fe
         * sets the active player ptr [0x2ebd0] = 0x2e7dc + index*0x84 and loads that
         * knight's per-turn move budget [0x2fa08] from record+0x56; when a turn's move
         * counter [0x2f9dc] reaches the budget the loop advances [0x2f9da] (0x40334)
         * and re-selects.  When the round wraps to 0 the "Next Day" daybreak fires
         * (0x4034a).  Lives = record+0x49 (new-game init = 5 @0x260a4); on a combat
         * death (HIT +0x50 <= 0) the win-resolution restores HIT to max (+0x54) and
         * decrements +0x49 (0x21434 = combatant-at-[0x2e0bc+0], 0x21446 = the one at
         * [0x2e0bc+4]).  When all players are out of lives the map loop reaches
         * GAME-OVER 0x21d5c. */
        {
            if (pc == 0x25f80u) {                      /* new-game roster build (loops up to [0x2e024]) */
                fprintf(g_log, "MP-SETUP fr=%d ic=%llu players[0x2e024]=%d gore[0x30518]=%d roster:",
                        g_cur_frame, (unsigned long long)g_icount,
                        (int16_t)r16(0x2e024), (int16_t)r16(0x30518));
                for (int i = 0; i < 4; i++) {
                    uint32_t b = 0x2e7dc + i*0x84;
                    fprintf(g_log, " rec%d{slot=%d pos=(%d,%d) lives=%d}", i,
                            (int)r32(b+0x36),
                            (int16_t)r16(b+0x7e), (int16_t)r16(b+0x80),
                            (int)r8(b+0x49));
                }
                fprintf(g_log, "\n");
            }
            else if (pc == 0x40410u) {                 /* TURN-SELECT 0x403fe: active player just chosen by rotation index */
                uint32_t idx = r16(0x2f9da);
                uint32_t b   = 0x2e7dc + idx*0x84;
                fprintf(g_log, "MP-TURN fr=%d ic=%llu rot[0x2f9da]=%u -> active=%06x slot=%d pos=(%d,%d) HIT=%d/%d lives=%d %s\n",
                        g_cur_frame, (unsigned long long)g_icount, idx, b,
                        (int)r32(b+0x36),
                        (int16_t)r16(b+0x7e), (int16_t)r16(b+0x80),
                        (int16_t)r16(b+0x50), (int16_t)r16(b+0x54),
                        (int)r8(b+0x49),
                        (r32(b+0x36)==4)?"(AI turn)":"(PLAYER turn)");
            }
            else if (pc == 0x4034au) {                 /* round wrapped -> "Next Day" daybreak */
                fprintf(g_log, "MP-DAYBREAK fr=%d ic=%llu day[0x2f9da]wrapped -> Next Day omen (round complete)\n",
                        g_cur_frame, (unsigned long long)g_icount);
            }
            else if (pc == 0x21434u) {                 /* combatant-1 ([0x2e0bc+0]) died: lives-- */
                uint32_t a0 = m68k_get_reg(NULL,M68K_REG_A0);
                fprintf(g_log, "MP-DEATH fr=%d ic=%llu combatant1 rec=%06x slot=%d lives %d -> %d (HIT restored to max %d)\n",
                        g_cur_frame, (unsigned long long)g_icount, a0,
                        (int)r32(a0+0x36), (int)r8(a0+0x49), (int)r8(a0+0x49)-1,
                        (int16_t)r16(a0+0x54));
            }
            else if (pc == 0x21446u) {                 /* combatant-2 ([0x2e0bc+4]) died: lives-- */
                uint32_t a1 = m68k_get_reg(NULL,M68K_REG_A1);
                fprintf(g_log, "MP-DEATH fr=%d ic=%llu combatant2 rec=%06x slot=%d lives %d -> %d (HIT restored to max %d)\n",
                        g_cur_frame, (unsigned long long)g_icount, a1,
                        (int)r32(a1+0x36), (int)r8(a1+0x49), (int)r8(a1+0x49)-1,
                        (int16_t)r16(a1+0x54));
            }
            else if (pc == 0x403b2u) {                 /* a player record is out of lives (+0x49<=0): counted toward all-dead */
                uint32_t a0 = r32(0x2ebd0);
                fprintf(g_log, "MP-FALLEN fr=%d ic=%llu active=%06x slot=%d lives=%d deadcount[0x2fa02]=%d/players=%d\n",
                        g_cur_frame, (unsigned long long)g_icount, a0,
                        (int)r32(a0+0x36), (int)r8(a0+0x49),
                        (int16_t)r16(0x2fa02)+1, (int16_t)r16(0x2e024));
            }
            else if (pc == 0x21d5cu) {                 /* all players dead -> GAME OVER */
                fprintf(g_log, "MP-GAMEOVER fr=%d ic=%llu *** all %d players out of lives -> game over (restart front-end) ***\n",
                        g_cur_frame, (unsigned long long)g_icount, (int16_t)r16(0x2e024));
            }
        }
    }
    g_prev_pc = pc;
    g_icount++;
    if (g_flow) {
        uint32_t page = pc >> 12;
        if (page != g_last_page) {
            char dis[256]; m68k_disassemble(dis, pc, M68K_CPU_TYPE_68000);
            fprintf(g_log, "FLOW pc=%06x (page %03x) icount=%llu  %s\n",
                    pc, page, (unsigned long long)g_icount, dis);
            g_last_page = page;
        }
    }
    if (g_trace_from && g_icount == g_trace_from) g_trace = g_trace_budget;
    if (g_trace > 0) {
        char dis[256];
        m68k_disassemble(dis, pc, M68K_CPU_TYPE_68000);
        if (g_traceregs)
            fprintf(g_log, "%06x: %-40s d0=%08x d1=%08x a0=%08x a1=%08x a4=%08x a5=%08x a7=%08x\n",
                pc, dis,
                (unsigned)m68k_get_reg(NULL,M68K_REG_D0),
                (unsigned)m68k_get_reg(NULL,M68K_REG_D1),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A0),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A1),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A4),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A5),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A7));
        else
        fprintf(g_log, "%06x: %-40s  d0=%08x a0=%08x a7=%08x sr=%04x\n",
                pc, dis,
                (unsigned)m68k_get_reg(NULL,M68K_REG_D0),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A0),
                (unsigned)m68k_get_reg(NULL,M68K_REG_A7),
                (unsigned)m68k_get_reg(NULL,M68K_REG_SR));
        g_trace--;
    }
    if (g_run_budget && g_icount > g_run_budget) halt("instruction budget exhausted");
}
int moon_illegal_hook(int opcode) {
    char dis[256]; uint32_t pc=m68k_get_reg(NULL,M68K_REG_PC);
    m68k_disassemble(dis, pc, M68K_CPU_TYPE_68000);
    /* The Mog engine probes the CPU for 68010+ features by EXECUTING a privileged
     * control instruction -- movec (opcode 0x4e7a read, 0x4e7b write) -- after
     * pointing the illegal vector ($10) at its own recovery stub.  On a real
     * 68000 the movec is illegal and traps to that stub; the game RELIES on this
     * trap (it is a self-decrypting CPU-detection routine that also single-steps
     * via the TRACE vector $24, so a chain of these probes runs from various
     * addresses incl. on-stack builders).  When the faulting opcode is a movec
     * and the game has armed a plausible (nonzero, in-RAM, not the boot guard)
     * $10 handler, let the normal 68000 illegal exception proceed (return 0 =
     * unhandled) so it vectors through $10 instead of halting.  Genuine runaways
     * into zero-fill (where $10 is 0 or 0x500) still halt. */
    if (g_os && (opcode == 0x4e7a || opcode == 0x4e7b)) {
        uint32_t v10 = r32(0x10);
        if (v10 != 0 && v10 != 0x500 && v10 < RAM_SIZE) {
            if (g_log && pc < 0x60000) fprintf(g_log,
                "ILLEGAL(movec-probe) opcode %04x at pc=%06x -> $10=%06x (proceed)\n",
                opcode&0xffff, pc, v10);
            return 0;  /* take the 68000 illegal exception via the installed vector */
        }
    }
    if (g_log) fprintf(g_log, "*** ILLEGAL opcode %04x at pc=%06x : %s\n", opcode&0xffff, pc, dis);
    halt("illegal instruction");
    return 0; /* genuine crash: stop */
}
int moon_trap_hook(int trap) {
    if (g_log) fprintf(g_log, "*** TRAP #%d at pc=%06x\n", trap, (unsigned)m68k_get_reg(NULL,M68K_REG_PC));
    return 0;
}

/* ----------------------------------------------------------------- names */
static const char *creg_name(uint32_t off) {
    switch (off & 0x1fe) {
        case 0x096: return "DMACON"; case 0x09a: return "INTENA"; case 0x09c: return "INTREQ";
        case 0x09e: return "ADKCON"; case 0x100: return "BPLCON0"; case 0x102: return "BPLCON1";
        case 0x104: return "BPLCON2"; case 0x108: return "BPL1MOD"; case 0x10a: return "BPL2MOD";
        case 0x08e: return "DIWSTRT"; case 0x090: return "DIWSTOP"; case 0x092: return "DDFSTRT";
        case 0x094: return "DDFSTOP"; case 0x080: return "COP1LCH"; case 0x082: return "COP1LCL";
        case 0x088: return "COPJMP1"; case 0x058: return "BLTSIZE"; case 0x040: return "BLTCON0";
        case 0x042: return "BLTCON1"; case 0x180: return "COLOR00";
    }
    if ((off&0x1fe)>=0x0e0 && (off&0x1fe)<0x100) return "BPLxPT";
    if ((off&0x1fe)>=0x120 && (off&0x1fe)<0x180) return "SPRxx";
    if ((off&0x1fe)>=0x180 && (off&0x1fe)<0x1c0) return "COLORxx";
    if ((off&0x1fe)>=0x0a0 && (off&0x1fe)<0x0e0) return "AUDxx";
    if ((off&0x1fe)>=0x040 && (off&0x1fe)<0x078) return "BLTxx";
    return "?";
}

/* ------------------------------------------------- copper + framebuffer */
/* Scanline-accurate, NON-destructive render of the current frame: walk the
 * live copper list in beam order into a private shadow register file, and emit
 * each display line with the register/palette state in effect at that line.
 * Handles mid-screen BPLCON0/palette splits and double-buffered lists. */
#define FB_W 384
#define FB_H 312

/* Sprite-position calibration (lores HSTART offset of fb x=0).  The Amiga
 * positions hardware sprites in the same hpos space as the bitplane display:
 * a sprite's HSTART lands on screen column (HSTART - origin) where the origin
 * is the display window's horizontal start.  The bitplane fetch is aligned to
 * DDFSTRT/DIWSTRT; for this game DIWSTRT_h == 0x81 and the pointer sprite lands
 * one display-pixel left of HSTART==DIWSTRT_h, so origin = (DIWSTRT&0xff)-1.
 * Overridable via --spritex for calibration without a rebuild. */
int g_sprite_xoff = -1000;   /* <-overridden by --spritex; -1000 = auto */

static inline void put_sprite_px(uint8_t *out, const uint16_t *pal,
                                 int oy, int sxp, int palidx) {
    uint16_t c = pal[palidx&31] & 0xfff;
    uint8_t *p = out + oy*FB_W*3 + sxp*3;
    p[0]=(uint8_t)(((c>>8)&0xf)*17); p[1]=(uint8_t)(((c>>4)&0xf)*17); p[2]=(uint8_t)((c&0xf)*17);
}

/* Composite the 8 OCS hardware sprites on top of the already-rendered bitplane
 * framebuffer.  `reg` is render_rgb's shadow register file (holds the live
 * SPRxPT pointers the copper left, the colour palette, and DIWSTRT).  We read
 * each sprite's data list from chip RAM exactly as Denise would: a chain of
 * {SPRxPOS, SPRxCTL} control pairs each followed by (VSTOP-VSTART) lines of two
 * data words, terminated by a {0,0} control pair.  For each visible line we plot
 * 16 pixels, colour index = bitA | (bitB<<1), index 0 transparent.  Sprite pair
 * 2N/2N+1 uses the colour bank COLOR(16 + N*4 .. +3) = palette indices 17..19,
 * 21..23, 25..27, 29..31.  When the ODD sprite of a pair has ATTACH set, the two
 * sprites combine into one 16-colour sprite (palette 16..31, idx = even_2bits |
 * (odd_2bits<<2)); we render the pair together in the even sprite's pass and skip
 * the odd one.  Sprites are processed 0..7 so a higher-numbered sprite draws on
 * top (matching Denise priority within the sprite group). */
static void composite_sprites(uint8_t *out, const uint16_t *reg,
                              int width, int vstart, int vstop) {
    if (!g_os) return;                              /* only the game uses HW sprites */
    (void)vstop;
    int diw_h = reg[0x08e>>1] & 0xff;               /* DIWSTRT horizontal start */
    int xorg  = (g_sprite_xoff > -1000) ? g_sprite_xoff : (diw_h - 1);

    /* The game drives SPRxPT from the COPPER list every frame (the same list at
     * COP1LC that holds the BPLxPT MOVEs); render_rgb's table-fallback zeroes
     * `cp` and skips the copper walk, so the SPRxPT MOVEs never reach reg[].
     * Harvest SPRxPT (and any copper-set sprite colours / SPRxPOS/CTL) here by
     * walking the copper list ourselves into a private sprite-pointer + palette
     * snapshot.  This is faithful: it is exactly the MOVEs the copper issues. */
    uint32_t sprpt[8];
    uint16_t pal[32];                               /* sprite colour banks 16..31 (palette idx) */
    for (int i=0;i<8;i++) sprpt[i] = (((uint32_t)reg[(0x120+i*4)>>1]<<16)|reg[(0x122+i*4)>>1]) & (RAM_SIZE-1);
    for (int i=0;i<32;i++) pal[i] = reg[(0x180 + i*2)>>1];
    {
        uint32_t cp = (((uint32_t)reg[0x080>>1]<<16)|reg[0x082>>1]) & (RAM_SIZE-1);
        int guard=0;
        while (cp && cp+4<=RAM_SIZE && guard<4000) {
            uint16_t w1=r16(cp), w2=r16(cp+2); guard++;
            if (w1 & 1) { if (w1==0xFFFF && w2==0xFFFE) break; cp+=4; continue; }  /* WAIT/SKIP */
            uint32_t r = w1 & 0x1fe; cp+=4;
            if (r==0x088) { cp=(((uint32_t)reg[0x080>>1]<<16)|reg[0x082>>1])&(RAM_SIZE-1); continue; }
            if (r==0x08a) { cp=(((uint32_t)reg[0x084>>1]<<16)|reg[0x086>>1])&(RAM_SIZE-1); continue; }
            if (r>=0x120 && r<0x140) {              /* SPRxPTH/PTL */
                int s=(r-0x120)/4;
                if (r&2) sprpt[s]=(sprpt[s]&0xffff0000)|w2; else sprpt[s]=(sprpt[s]&0xffff)|((uint32_t)w2<<16);
                sprpt[s]&=(RAM_SIZE-1);
            } else if (r>=0x1a0 && r<0x1c0) {       /* COLOR16..31 (sprite banks) */
                pal[(r-0x180)/2] = w2;
            }
        }
    }

    static uint32_t sprdbg_n = 0;
    for (int s=0; s<8; s++) {
        uint32_t sp = sprpt[s];
        if (g_rdbg && g_log && sprdbg_n < 240) {    /* rate-limit (~first 30 renders) */
            sprdbg_n++;
            uint16_t pos = (sp && sp+4<=RAM_SIZE)?r16(sp):0, ctl=(sp && sp+4<=RAM_SIZE)?r16(sp+2):0;
            fprintf(g_log, "SPR%d pt=%06x pos=%04x ctl=%04x vstart=%d vstop=%d hstart=%d xorg=%d col=%04x,%04x,%04x\n",
                    s, sp, pos, ctl, (pos>>8)|((ctl&4)?256:0), (ctl>>8)|((ctl&2)?256:0),
                    ((pos&0xff)<<1)|(ctl&1), xorg,
                    pal[16+(s/2)*4+1], pal[16+(s/2)*4+2], pal[16+(s/2)*4+3]);
        }
        if (sp==0) continue;
        int bank = 16 + (s/2)*4;                    /* COLOR bank base for this sprite pair */

        /* Walk the chained sprite images for this sprite within the frame. */
        for (int img=0; img<16 && sp && sp+4<=RAM_SIZE; img++) {
            uint16_t pos = r16(sp), ctl = r16(sp+2);
            if (pos==0 && ctl==0) break;            /* end of list */
            int vstart_s = (pos>>8) | ((ctl&0x04)?0x100:0);
            int vstop_s  = (ctl>>8) | ((ctl&0x02)?0x100:0);
            int hstart   = ((pos&0xff)<<1) | (ctl&1);
            int attached = (ctl & 0x80) ? 1 : 0;    /* odd sprite attaches to N-1 */
            int nlines   = vstop_s - vstart_s; if (nlines<0) nlines=0;
            int sx0      = hstart - xorg;           /* screen x of leftmost sprite pixel */
            uint32_t datap = sp + 4;                /* first data line */

            /* If THIS even sprite's odd partner is ATTACHed, render the 16-colour
             * pair here by reading the partner's data words in lock-step, then we
             * still advance past our own data; the odd sprite's own pass will be a
             * no-op for those lines (it sees attached and we skip plotting there).
             * Detect the partner's attach by peeking its first control word. */
            int pair_attach = 0; uint32_t odp = 0;
            if (!(s&1)) {
                uint32_t op = sprpt[s+1];
                if (op && op+4<=RAM_SIZE) {
                    uint16_t octl = r16(op+2);
                    if (octl & 0x80) { pair_attach = 1; odp = op + 4; }  /* odd data follows its ctl pair */
                }
            }

            for (int l=0; l<nlines; l++) {
                if (datap+4 > RAM_SIZE) break;
                uint16_t da = r16(datap), db = r16(datap+2); datap += 4;
                uint16_t oa = 0, ob = 0;
                if (pair_attach && odp+4 <= RAM_SIZE) { oa = r16(odp); ob = r16(odp+2); odp += 4; }
                int oy = (vstart_s + l) - vstart;
                if (oy < 0 || oy >= FB_H) continue;
                for (int px=0; px<16; px++) {
                    int bit = 15 - px;
                    int sxp = sx0 + px;
                    if (sxp < 0 || sxp >= width || sxp >= FB_W) continue;
                    if (pair_attach) {
                        int e2 = ((da>>bit)&1) | (((db>>bit)&1)<<1);
                        int o2 = ((oa>>bit)&1) | (((ob>>bit)&1)<<1);
                        int idx16 = e2 | (o2<<2);
                        if (idx16==0) continue;     /* fully transparent */
                        put_sprite_px(out, pal, oy, sxp, 16 + idx16);
                    } else if (s&1 && attached) {
                        /* odd sprite already drawn as part of the even pair */
                        continue;
                    } else {
                        int idx2 = ((da>>bit)&1) | (((db>>bit)&1)<<1);
                        if (idx2==0) continue;      /* transparent */
                        put_sprite_px(out, pal, oy, sxp, bank + idx2);
                    }
                }
            }
            sp = datap;                             /* next image's control pair */
        }
    }
}

/* DIAG (livedump): last render_rgb display-source state, for the Stonehenge give-cutscene
 * artifact hunt -- which buffer/stride was displayed + whether front-recover fired. */
static uint32_t g_dbg_cp=0, g_dbg_stride=0, g_dbg_dispbase=0, g_dbg_recover=0; static int g_dbg_nplanes=0;
static int g_render_garbled = 0;   /* set by render_rgb: the displayed give-cutscene buffer is a mid-composite garble */

/* Vertical coherence of planes 2..nplanes-1 of a packed bitplane buffer: the fraction of
 * NONZERO bytes equal to the byte one row above.  A genuine decoded bitmap (trees, text,
 * gradients) is high; a torn/garbage composite is low.  Returns 0 if too little content to
 * judge.  Mirrors the VCOH macro in the front-recover block, as a reusable function. */
static double plane_coh(uint32_t base, uint32_t psz, int bytew, int rows, int nplanes) {
    long eqnz=0, nz=0;
    for (int pl=2; pl<nplanes && pl<5; pl++) {
        uint32_t b = base + (uint32_t)pl*psz;
        for (int y=1; y<rows; y++) {
            uint32_t r0=b+(uint32_t)(y-1)*bytew, r1=b+(uint32_t)y*bytew;
            for (int k=0;k<bytew;k++){ uint8_t v=r8(r1+k); if(v){ nz++; if(v==r8(r0+k)) eqnz++; } }
        }
    }
    return (nz<200)? 0.0 : (double)eqnz/(double)nz;
}
static int render_rgb(uint8_t *out, int *out_w, int *out_h) {
    uint16_t reg[256];
    memcpy(reg, g_custom, sizeof(reg));        /* shadow; never touch live regs */
    g_render_garbled = 0;                       /* recomputed below for the give-cutscene buffers */
    uint32_t bplptr[6];
    for (int i=0;i<6;i++) bplptr[i] = ((uint32_t)reg[(0x0e0+i*4)>>1]<<16)|reg[(0x0e2+i*4)>>1];

    /* geometry */
    int ddfstrt = reg[0x092>>1]&0xfc, ddfstop = reg[0x094>>1]&0xfc;
    int nfetch  = ddfstop > ddfstrt ? ((ddfstop-ddfstrt)/8 + 1) : 20;
    int width   = nfetch*16; if (width<16||width>FB_W) width = 320;
    int vstart  = reg[0x08e>>1] >> 8;
    uint16_t diwstop = reg[0x090>>1];
    int vstop   = (diwstop>>8) | ((diwstop & 0x8000) ? 0 : 0x100);
    if (vstop<=vstart || vstop-vstart>FB_H) { vstart=0x2c; vstop=0x2c+256; }
    int height  = vstop - vstart; if (height>FB_H) height=FB_H;
    int bytew   = width/8;

    uint32_t cp = ((uint32_t)reg[0x080>>1]<<16)|reg[0x082>>1];

    /* `program`/`Mog` point COP1LC at a BPLxPT pointer table rather than a
     * conventional copper list: the block at COP1LC is laid out exactly like a
     * copper BPLxPT MOVE sequence (hi, lo per plane) BUT with the register
     * selector words left zero -- i.e. {0000, ptrhi, 0000, ptrlo} repeated per
     * plane.  Parsed as a normal copper list those MOVEs go to register 0x000
     * (harmless no-ops) and the bitplanes never get pointers, so the screen is
     * black.  Detect this packed-pointer-table shape and load BPLxPT directly.
     * Per plane the 8-byte record is {sel=0000, ptrhi, sel=0000, ptrlo}, so the
     * i-th plane pointer = (word@COP1LC+2+i*8 << 16) | word@COP1LC+6+i*8.  (A real
     * copper list -- e.g. the cracktro's -- has nonzero selectors, so it is left
     * to the normal walk below; this fallback only fires for the zero-selector
     * table.) */
    if (g_os && cp && cp < RAM_SIZE) {
        int nplanes_hint = (reg[0x100>>1]>>12)&7; if (nplanes_hint==0) nplanes_hint=5;
        /* Validate the table by the VALUE words (ptr hi@+2, lo@+6 per 8-byte
         * record), NOT the selector words: the foreground rebuilds the selectors
         * concurrently, so they tear between frames -- but the value pointers are
         * a stable, contiguous, equal-stride bitplane set.  Require: every plane
         * pointer is a nonzero chip-RAM address, and consecutive planes share one
         * positive stride (a packed multi-plane buffer).  That shape is unique to
         * this game's table and never matches the cracktro's real copper list. */
        uint32_t p[5]; int looks_table = (nplanes_hint>=2);
        for (int i=0;i<nplanes_hint && i<5;i++)
            p[i] = (((uint32_t)r16(cp+2+i*8)<<16) | r16(cp+6+i*8)) & 0xffffff;
        uint32_t stride = (nplanes_hint>=2) ? (p[1]-p[0]) : 0;
        for (int i=0;i<nplanes_hint && i<5 && looks_table;i++) {
            if (p[i]==0 || p[i]>=RAM_SIZE) looks_table = 0;
            if (i>0 && (p[i]-p[i-1])!=stride) looks_table = 0;
        }
        if (looks_table && (stride==0 || (stride>=0x400 && stride<0x40000))) {
            for (int i=0;i<6;i++)
                bplptr[i] = (((uint32_t)r16(cp+2+i*8)<<16) | r16(cp+6+i*8)) & (RAM_SIZE-1);
            g_dbg_cp = cp; g_dbg_stride = stride; g_dbg_nplanes = nplanes_hint;   /* DIAG */
            g_dbg_dispbase = bplptr[0]; g_dbg_recover = 0;
            cp = 0;                                       /* skip the copper walk below */

            /* FRONT-BUFFER RECOVERY.  `program` decodes each background screen as a
             * COMPLETE 5-plane bitmap into a slot of its screen-buffer REGISTRY
             * (table of bases at 0x22462, stride 0x9c40 == one 320x200x5 screen; the
             * decompressor at 0x2c7c6 writes the whole bitmap there).  The visible
             * double-buffer (where the table above points) is meant to be composited
             * by copying the active registry slot into it -- but in this port the
             * higher planes of that copy lag, so the directly-pointed buffer shows
             * planes 0-1 of the current screen with stale/garbage data in planes 2-4
             * (the sheared/noisy image).  The clean, fully-decoded screen still lives
             * in the registry.  Recover it: among registry slots whose PLANE 0 is
             * byte-identical to the displayed buffer's (== the SAME screen), pick the
             * one whose upper planes (2..n) are the most VERTICALLY COHERENT -- a
             * real decoded bitmap has strongly correlated adjacent rows, whereas a
             * torn/partial copy does not.  Only substitute when that best slot is
             * clearly more coherent than the displayed buffer itself, so a screen
             * whose visible copy is already whole (or the cracktro, which never uses
             * this zero-selector table at all) is left untouched. */
            if (g_recover && stride==0x1f40 && nplanes_hint>=3) {
                uint32_t dispbase = bplptr[0];
                uint32_t psz = stride;                    /* bytes per plane (8000) */
                int rows = (int)(psz / (uint32_t)bytew); if (rows>FB_H) rows=FB_H;
                /* Content-weighted vertical coherence of the upper planes (2..n-1):
                 * the fraction of NONZERO bytes that equal the byte one row above.
                 * A genuine decoded bitmap has structure that persists vertically
                 * (tree trunks, gradients) so this is high; a torn/partial copy has
                 * incoherent rows so it is low.  Counting only nonzero bytes avoids
                 * being fooled by an all-zero (= stale/cleared) plane, which would
                 * otherwise look "perfectly coherent".  Returns 0 if a plane set is
                 * essentially empty (too little content to judge). */
                #define VCOH(base) ({ \
                    long _eqnz=0,_nz=0; \
                    for (int _pl=2; _pl<nplanes_hint && _pl<5; _pl++){ \
                        uint32_t _b=(base)+(uint32_t)_pl*psz; \
                        for (int _y=1;_y<rows;_y++){ uint32_t _r0=_b+(uint32_t)(_y-1)*bytew, _r1=_b+(uint32_t)_y*bytew; \
                            for (int _k=0;_k<bytew;_k++){ uint8_t _v=r8(_r1+_k); if(_v){ _nz++; if(_v==r8(_r0+_k)) _eqnz++; } } } } \
                    (_nz<200)? 0.0 : (double)_eqnz/(double)_nz; })
                double disp_coh = VCOH(dispbase);
                uint32_t best_slot = 0; double best_coh = disp_coh + 0.03;  /* require a clear win */
                for (int s=0;s<8;s++) {
                    uint32_t slot = r32(0x22462 + s*4);
                    if (slot==0 || slot>=RAM_SIZE || (slot+5*psz)>=RAM_SIZE) continue;
                    if (slot==dispbase) continue;
                    /* plane 0 must be (near-)identical: this is the SAME screen.  The
                     * displayed copy's OWN plane 0 may differ from the registry master
                     * by a few percent (its partial composite), so require >=88% of
                     * sampled bytes equal rather than exact identity. */
                    int eq0=0, n0=0;
                    for (uint32_t k=0;k<psz;k+=64) { n0++; if (r8(slot+k)==r8(dispbase+k)) eq0++; }
                    if (n0==0 || eq0*100 < n0*88) continue;
                    double c = VCOH(slot);
                    if (c > best_coh) { best_coh = c; best_slot = slot; }
                }
                #undef VCOH
                if (best_slot) {
                    for (int i=0;i<6 && i<nplanes_hint;i++) bplptr[i] = best_slot + (uint32_t)i*stride;
                    g_dbg_recover = best_slot;   /* DIAG: front-recover fired */
                    if (g_rdbg && g_log)
                        fprintf(g_log, "FRONT-RECOVER: disp=%06x coh=%.3f -> registry slot base=%06x coh=%.3f\n",
                                dispbase, disp_coh, best_slot, best_coh);
                }
            }

            /* EMPTY-BACKBUFFER RECOVERY (the final intro story scene).
             * `program`/`Mog` show their attract scenes through a fixed double-
             * buffer pair: bases 0x6bdfa and 0x75a3c, contiguous (0x6bdfa+0x9c40
             * == 0x75a3c), each a 5-plane 320x200 screen (stride 0x1f40).  The
             * engine alternates the displayed base in the 0x7f6ae pointer table
             * every frame, but for most attract scenes it only ever composites the
             * decoded screen into ONE of the two halves -- the other stays blank.
             * For the working scenes our vblank-aligned capture happens to land on
             * the filled half; for the final intro scene (the Stonehenge daybreak
             * before the title) it consistently lands on the BLANK half, so that
             * scene renders almost entirely black even though the full screen is
             * sitting in the sibling buffer.  Detect exactly that case -- the table
             * points at one of the known display halves, that half is essentially
             * empty, and a fuller source exists -- and display that source instead.
             *
             * The source is either the sibling display half OR the engine's SCREEN-
             * REGISTRY master slot 0 ([0x22462]).  `program` decodes/composites each
             * attract screen into registry slot 0, then blits slot 0 into BOTH display
             * halves (compose routine 0x273b6: A=[0x22462] -> D=[0xbe44]=0x6bdfa and
             * D=[0xbe48]=0x75a3c).  For the FINAL legend scene -- the "The druids sent
             * their best knights to Stonehenge ... Quest for the MOONSTONE" text over
             * the teal vortex -- the engine first copies the vortex into both halves,
             * then renders the legend TEXT into registry slot 0 ONLY (text-blit dest
             * 0x80000 == slot 0), intending a final slot-0 -> display blit that does
             * not complete in this port (the displayed half 0x6bdfa is left cleared).
             * So the sibling half holds the vortex WITHOUT the text, while slot 0 holds
             * the complete vortex+text master.  Pick the FULLER of {sibling, slot 0} so
             * the legend text is not lost.  Conservative by construction: a scene whose
             * visible half is already populated (every other attract screen), a blank
             * inter-scene frame (no fuller source), and the cracktro (g_os=0, never
             * reaches this zero-selector table path) are all left untouched. */
            /* CF-NZ (--cflog) diagnostic: log the nz-triplet (displayed half, sibling half,
             * registry master) UNGATED, so we can see it at the win/"The End" screens where the
             * real recovery is gated off.  Tests the "transition = BOTH halves empty" hypothesis. */
            if (g_cflog && g_log && stride==0x1f40 && nplanes_hint>=5 &&
                (bplptr[0]==0x6bdfau || bplptr[0]==0x75a3cu)) {
                uint32_t d=bplptr[0], sb=(d==0x6bdfau)?0x75a3cu:0x6bdfau, ps=stride, m=r32(0x22462);
                #define NZF(base) ({ long _nz=0,_t=0; for(int _p=0;_p<5;_p++){uint32_t _b=(base)+(uint32_t)_p*ps; \
                    for(uint32_t _k=0;_k<ps;_k+=16){_t++; if(r8(_b+_k))_nz++;}} (_t?(double)_nz/(double)_t:0.0);})
                double mnz = (m && (m%2==0) && (m+5*ps)<RAM_SIZE) ? NZF(m) : -1.0;
                fprintf(g_log,"CF-NZ fr=%d bs=%d scene=%u disp=%06x(%.2f) sib=%06x(%.2f) master=%06x(%.2f)\n",
                    g_cur_frame,g_blt_busy_scope,(unsigned)r32(0x2fb1cu),d,NZF(d),sb,NZF(sb),m,mnz);
                #undef NZF
            }
            if (!g_rawcapture && (g_blt_busy_scope || r32(0x22462u) == 0x80000u)
                && stride==0x1f40 && nplanes_hint>=5 &&
                (bplptr[0]==0x6bdfau || bplptr[0]==0x75a3cu)) {
                /* ROOT-CAUSE GATE (2026-06-28): recover ONLY while the attract COMPOSE model is the
                 * active renderer -- detected by EITHER g_blt_busy_scope==1 (the first attract pass)
                 * OR the registry master slot0 ([0x22462]) being the fixed compose master 0x80000.
                 * The slot0==0x80000 term is what makes this bulletproof across attract LOOPS and the
                 * post-victory `program` RELAUNCH: g_blt_busy_scope is set only at boot and cleared at
                 * Mog launch, so a relaunched/looped attract has bs==0 and would lose recovery for its
                 * bright compose scenes (e.g. the "Quest for the MOONSTONE" legend, whose overlay text
                 * lives only in the master) -- but slot0 is still 0x80000 there, so recovery stays on.
                 * Verified safe: every in-game/outro screen has slot0 != 0x80000 (win=0x4673c,
                 * "The End"=0x93880, gameplay=0x4673c), so it is structurally excluded; the only bs==0
                 * frames with slot0==0x80000 are the attract->demo handoff, where disp is FULL so the
                 * disp_nz<0.08 test never fires.  Traced WHY this is the correct gate, not the old
                 * per-screen opt-outs (g_delivery_win/g_theend_win):
                 *  - The empty-displayed-half problem is INTRINSIC to `program`'s attract model: it
                 *    composites each scene into the registry master (0x80000) then copies master->both
                 *    halves (pc 0x2740e) a few frames later; for ~1-3 frames the copper already points at
                 *    a half the copy hasn't filled, so it reads empty.  In this phase the MASTER always
                 *    holds the CURRENT scene, so substituting it (or the equally-current sibling) is
                 *    always safe.
                 *  - In-game/outro (g_blt_busy_scope==0) screens draw DIRECTLY into a half and are often
                 *    legitimately SPARSE (bg8.piv "You have completed the quest"; "The End").  There the
                 *    master/sibling hold a STALE different scene, so any nz-based recovery wrongly swaps
                 *    in that stale buffer -- the win-screen/"The End" garbles.  Confirmed by trace: the
                 *    displayed-half nz (0.05-0.08) is INDISTINGUISHABLE from an attract transition
                 *    (0.00-0.05), so no content test can separate them -- only the phase can.
                 * Gating on the phase makes the per-screen g_delivery_win/g_theend_win opt-outs
                 * unnecessary (bs==0 already excludes every in-game/outro screen). */
                uint32_t disp = bplptr[0];
                uint32_t sib  = (disp==0x6bdfau) ? 0x75a3cu : 0x6bdfau;
                uint32_t psz  = stride;
                /* sampled nonzero fraction across all 5 planes of a buffer base */
                #define NZFRAC(base) ({ long _nz=0,_tot=0; \
                    for (int _pl=0;_pl<5;_pl++){ uint32_t _b=(base)+(uint32_t)_pl*psz; \
                        for (uint32_t _k=0;_k<psz;_k+=16){ _tot++; if (r8(_b+_k)) _nz++; } } \
                    (_tot? (double)_nz/(double)_tot : 0.0); })
                double disp_nz = NZFRAC(disp);
                double sib_nz  = NZFRAC(sib);
                /* registry master slot 0 -- the scene the engine composites + blits
                 * from; for the legend scene this is the only buffer with the text. */
                uint32_t slot0 = r32(0x22462);
                int slot0_ok = (slot0 && stride && (slot0 % 2 == 0) &&
                                slot0 != disp && slot0 != sib &&
                                (slot0 + 5*psz) < RAM_SIZE);
                double slot0_nz = slot0_ok ? NZFRAC(slot0) : 0.0;
                #undef NZFRAC
                if (disp_nz < 0.08) {
                    /* choose the fuller source; prefer the registry master when it is
                     * at least as full as the sibling (it carries any text overlay). */
                    uint32_t src = 0; double src_nz = 0.0;
                    if (sib_nz > 0.35)               { src = sib;   src_nz = sib_nz; }
                    if (slot0_nz >= src_nz && slot0_nz > 0.35) { src = slot0; src_nz = slot0_nz; }
                    if (src) {
                        for (int i=0;i<6;i++) bplptr[i] = src + (uint32_t)i*stride;
                        if (g_rdbg && g_log)
                            fprintf(g_log, "BACKBUF-RECOVER fr=%d disp=%06x(nz=%.2f) empty -> src=%06x(nz=%.2f) [sib=%06x(%.2f) slot0=%06x(%.2f)]\n",
                                    g_cur_frame, disp, disp_nz, src, src_nz, sib, sib_nz, slot0, slot0_nz);
                    }
                }
            }
            /* GIVE-CUTSCENE garbled-frame DETECT (2026-06-23): the give-item ritual's composite to the
             * displayed double-buffer half lags, so the half briefly holds the stale prior screen in
             * planes 0-1 over garbage planes 2-4.  No buffer holds a clean image mid-composite, so we
             * can't recover one -- instead FLAG the frame as garbled and let capture_frame hold the last
             * good frame (the black fade) until the real cutscene composites, so the garbage is never
             * shown.  In-game only; the half must hold real content (dfill>0.25, not an empty transition)
             * AND be visually incoherent (coh<0.45 = genuinely garbled, not a clean screen). */
            if (!g_rawcapture && !g_blt_busy_scope && stride==0x1f40u && nplanes_hint>=5 &&
                (bplptr[0]==0x6bdfau || bplptr[0]==0x75a3cu)) {
                uint32_t sc = r32(0x2fb1cu);
                if (sc == 3u) {                       /* give-ritual screen: garbage bitplanes (low coherence) */
                    long nz=0, tot=0;
                    for (int pl=0; pl<5; pl++){ uint32_t bb=bplptr[0]+(uint32_t)pl*stride;
                        for (uint32_t k=0;k<stride;k+=16){ tot++; if (r8(bb+k)) nz++; } }
                    double dfill = tot ? (double)nz/(double)tot : 0.0;
                    if (dfill > 0.25 &&
                        plane_coh(bplptr[0], stride, bytew, (int)(stride/(uint32_t)bytew), nplanes_hint) < 0.45)
                        g_render_garbled = 1;
                }
                else if (sc == 9u) {
                    /* moonstone-DELIVERY garbled map, palette-keyed (see g_delivery_win / g_map_pal). */
                    if (g_delivery_win > 0) {
                        int diff = 0;
                        for (int i = 0; i < 16; i++)
                            if ((reg[(0x180 + i*2)>>1] & 0xfff) != g_map_pal[i]) diff++;
                        g_dbg_paldiff = diff;
                        g_dbg_coh = plane_coh(bplptr[0], stride, bytew, (int)(stride/(uint32_t)bytew), nplanes_hint);
                        /* This scene-9 frame is the bg8.piv "You have completed the quest" WIN screen.
                         * With the win-screen auto-forward removed (see the 0x22fd0 hook) the screen no
                         * longer races into the relaunch, so it renders STABLY -- SHOW it (do not flag it
                         * garbled).  The palette diff is just bg8.piv's own palette.  (g_dbg_paldiff/coh
                         * are still computed above for the diagnostic log.) */
                    } else {
                        for (int i = 0; i < 16; i++) g_map_pal[i] = reg[(0x180 + i*2)>>1] & 0xfff;
                    }
                }
            }
        }
    }
    if (g_rdbg && g_log) {
        static uint32_t rdbg_n = 0;
        if (rdbg_n < 400) {  /* rate-limit: extended to trace the moonstone-delivery palette/bitplane evolution */
            fprintf(g_log, "RENDER fr=%d scene=%u bpl0=%06x c1=%03x c2=%03x c9=%03x cA=%03x | cop1lc=%06x planes=%d\n",
                    g_cur_frame, (unsigned)r32(0x2fb1cu), bplptr[0],
                    reg[0x182>>1]&0xfff, reg[0x184>>1]&0xfff, reg[0x192>>1]&0xfff, reg[0x194>>1]&0xfff,
                    ((uint32_t)reg[0x080>>1]<<16)|reg[0x082>>1], (reg[0x100>>1]>>12)&7);
            rdbg_n++;
        }
    }
    g_disp_base = bplptr[0];
    /* DELIVERY-RENDER (live diag): why doesn't the scene-9 garble gate fire on the 2nd map?  Log the
     * actual display params whenever a delivery is active and the engine reports scene 9. */
    if (g_delivery_win > 0 && g_log && r32(0x2fb1cu) == 9u) {
        static uint32_t drn = 0;
        if (drn++ < 300) {
            int pdiff = 0;
            for (int i = 0; i < 16; i++) if ((reg[(0x180+i*2)>>1]&0xfff) != g_map_pal[i]) pdiff++;
            fprintf(g_log, "DELIVERY-RENDER fr=%d bpl0=%06x cp=%06x blt=%d paldiff=%d win=%d garb=%d\n",
                    g_cur_frame, bplptr[0], cp, g_blt_busy_scope, pdiff, g_delivery_win, g_render_garbled);
        }
    }
    uint16_t w1=0,w2=0; int have=0;
    int guard=0;
    for (int line=0; line<vstop && guard<20000; line++) {
        /* apply copper MOVEs scheduled at/above this beam line */
        for (;;) {
            if (cp==0) break;                      /* no copper list (END / table fallback) */
            if (!have) { w1=r16(cp); w2=r16(cp+2); have=1; }
            if (w1 & 1) {                          /* WAIT/SKIP */
                if (w1==0xFFFF && w2==0xFFFE) { cp=0; break; }
                int wl = (w1>>8) & 0xff;           /* wait vpos (ignore h) */
                if (wl > line) break;              /* not yet */
                cp += 4; have=0;                   /* wait satisfied, consume */
                continue;
            }
            uint32_t r = w1 & 0x1fe; cp += 4; have=0; guard++;
            if (r==0x088) { cp=((uint32_t)reg[0x080>>1]<<16)|reg[0x082>>1]; continue; }
            if (r==0x08a) { cp=((uint32_t)reg[0x084>>1]<<16)|reg[0x086>>1]; continue; }
            if (r>=0x0e0 && r<0x100) {
                int p=(r-0x0e0)/4; if (r&2) bplptr[p]=(bplptr[p]&0xffff0000)|w2; else bplptr[p]=(bplptr[p]&0xffff)|((uint32_t)w2<<16);
            }
            reg[r>>1]=w2;
            if (cp==0) break;
        }
        if (line < vstart) continue;
        int oy = line - vstart; if (oy<0||oy>=FB_H) continue;
        int planes = (reg[0x100>>1]>>12)&7;
        int16_t mod1=(int16_t)reg[0x108>>1], mod2=(int16_t)reg[0x10a>>1];
        uint8_t *orow = out + oy*FB_W*3;
        for (int x=0;x<width;x++) {
            int byte=x>>3, bit=7-(x&7), idx=0;
            for (int i=0;i<planes;i++) idx |= ((r8(bplptr[i]+byte)>>bit)&1)<<i;
            uint16_t c = reg[(0x180 + (idx&31)*2)>>1] & 0xfff;
            orow[x*3+0]=(uint8_t)(((c>>8)&0xf)*17);
            orow[x*3+1]=(uint8_t)(((c>>4)&0xf)*17);
            orow[x*3+2]=(uint8_t)(( c    &0xf)*17);
        }
        for (int x=width;x<FB_W;x++){ orow[x*3]=orow[x*3+1]=orow[x*3+2]=0; }
        /* advance bitplane pointers by one fetched line (DMA increment) */
        if (planes>0) for (int i=0;i<planes;i++) bplptr[i] += bytew + ((i&1)?mod2:mod1);
    }
    /* Composite hardware sprites (pointer + any others) on top of the bitplanes. */
    composite_sprites(out, reg, width, vstart, vstop);
    *out_w = width; *out_h = height;
    return 0;
}

static uint8_t g_fb[FB_W*FB_H*3];

/* Vblank-aligned capture: the real Amiga copper latches COP1LC and the bitplane
 * pointers at the top of the display.  The game double-buffers and rebuilds the
 * copper/buffer pointer table from the foreground, so a render taken at an
 * arbitrary instruction boundary catches a torn list (= noise).  We therefore
 * snapshot the framebuffer at the VBlank instant each frame (when the displayed
 * list is the completed one) into g_cap, and `--dump` writes that snapshot. */
static uint8_t g_cap[FB_W*FB_H*3];
static int     g_cap_w = 320, g_cap_h = 256, g_cap_valid = 0;
static uint8_t g_cap_tmp[FB_W*FB_H*3];
static int     g_livedump = 0;   /* F10 toggles: write every displayed frame to livedump_NNNN.ppm next to the exe
                                  * (captures live-only transition glitches the instant-HLE headless path can't reproduce) */

/* Count of DISTINCT NON-ZERO colours currently in the COLOR palette (0..32).
 * A steadily-displayed attract/title/menu screen uses a rich palette (typically
 * 14..31 distinct colours); a brief scene TRANSITION resets the palette while the
 * next screen's bitplanes are still being decoded into the buffer, collapsing it
 * to a degenerate state (all black, or every used entry forced to one flash
 * colour such as white) -- distinct<=3.  Capturing during that window yields the
 * garbled all-black / stark black&white "mid-decode" frames the playtester saw. */
static int palette_distinct_nonzero(void) {
    uint16_t seen[32]; int n=0;
    for (int i=0;i<32;i++) {
        uint16_t c = g_custom[(0x180+i*2)>>1] & 0xfff;
        if (!c) continue;
        int f=0; for (int j=0;j<n;j++) if (seen[j]==c) { f=1; break; }
        if (!f) seen[n++]=c;
    }
    return n;
}

/* Fraction (0..1) of NON-BLACK pixels in a freshly rendered FB_W x h frame.
 * Used to tell a real scene from a mid-clear / black-out transition. */
static double frame_ink_fraction(const uint8_t *fb, int h) {
    long nz=0, tot=0;
    if (h<=0 || h>FB_H) h = FB_H;
    for (int y=0;y<h;y++) {
        const uint8_t *row = fb + (size_t)y*FB_W*3;
        for (int x=0;x<FB_W;x+=2) {                 /* sample every other column */
            tot++;
            if (row[x*3] | row[x*3+1] | row[x*3+2]) nz++;
        }
    }
    return tot ? (double)nz/(double)tot : 0.0;
}

/* TRANSITION HOLD: hold the last fully-rendered capture only while a scene change is
 * genuinely MID-DECODE -- the palette has collapsed to a degenerate state (<=3 distinct
 * non-zero colours) AND the displayed buffer is being wiped (almost no ink), i.e. the
 * stark black / black&white "switching-over" frames.  This is bounded very short so a
 * sustained blank (e.g. the ~1s Mog-load gap) still shows black.
 *
 * CRUCIAL: a LIGHTNING FLASH (the intro's platform-ritual / red-knight crashes, and
 * combat hit-flashes) ALSO collapses the palette to one bright colour -- but the scene
 * BITPLANES stay fully drawn, so the rendered frame is BRIGHT/FULL (high ink), not being
 * cleared.  By additionally requiring the frame to be near-empty, flashes are shown
 * faithfully while only the true mid-clear garbage is held.  g_os-gated; cracktro never
 * takes this path. */
#define TRANSITION_HOLD_MAX 8
#define TRANSITION_HOLD_INK 0.06   /* hold only when <6% of pixels are lit (mid-clear) */
#define GARBLE_HOLD_MAX 150        /* give-ritual (scene 3): cap (~3s) fail-safe against a mis-detect */
#define GARBLE_HOLD_DELIVERY 1400  /* moonstone delivery black-out cap per continuous held run; > the 1200-frame
                                    * delivery window (g_delivery_win) so the window, not this cap, bounds it. */
static int g_hold_run = 0;
static int g_garble_hold = 0;

static int write_ppm(const char *path, const uint8_t *fb, int w, int h);  /* fwd (DLDUMP diag) */
static void capture_frame(void) {
    int w,h;
    if (g_os && g_cap_valid) {
        memset(g_cap_tmp,0,sizeof(g_cap_tmp));
        render_rgb(g_cap_tmp,&w,&h);
        g_firewait_hot = 0; g_mappoll_hot = 0;   /* render_rgb consumed them; reset for the next frame's hooks */
        /* BOOT HOLD: at power-on the attract display turns on (bpl0=0x75a3c, copper table 0x7f6ae) ~13
         * frames before any content composites, showing the uninitialised buffer -- a near-black field of
         * sparse red/green specks -- then black through the `program` load, until the first real logo
         * (Mindscape).  Force black from the first captured frame until that first substantial-content
         * frame appears, so the specks + load gap are clean black.  One-shot (g_boot_done) so it only ever
         * affects power-on, never an in-game near-empty screen. */
        { static int g_boot_done = 0;
          if (!g_boot_done) {
              if (frame_ink_fraction(g_cap_tmp, h) > 0.03) g_boot_done = 1;   /* first logo composited */
              else { memset(g_cap, 0, sizeof(g_cap)); g_cap_w = w; g_cap_h = h; g_cap_valid = 1; return; }
          }
        }
        if (g_delivery_win > 0) g_delivery_win--;   /* delivery window counts down per displayed frame */
        int dlog = (g_delivery_win > 0 && g_log);   /* CAPFRAME diag: which branch does capture take live? */
        int tfill = 0;                              /* per-mille nonzero of the fresh render (body, not row0) */
        if (g_delivery_win > 0) { long nz=0,tot=0; for (int k=0;k<FB_W*FB_H*3;k+=48){ tot++; if (g_cap_tmp[k]) nz++; } tfill=(int)(tot?nz*1000/tot:0); }
        unsigned dsc = dlog ? (unsigned)r32(0x2fb1cu) : 0;
        /* DELIVERY display policy: the window now only ENDS itself when the season cutscene composites
         * to the display double-buffer 0x6bdfa (scene 0, tfill>20).  The old per-phase garble blackouts
         * ("first map"/"second map" -> BLACK) are GONE -- both were the SAME BACKBUF-RECOVER bug
         * (substituting the stale 0x80000 compose master), now gated off during delivery (see render_rgb).
         * The win screen, the disk prompt, and the "ceremony / Loading" relaunch screen all render clean. */
        if (g_delivery_win > 0) {
            uint32_t sc = r32(0x2fb1cu);
            if (sc == 0u && g_disp_base == 0x6bdfau && tfill > 20)
                g_delivery_win = 0;                 /* cutscene is compositing -> delivery over */
            /* No garble blackout here any more.  BOTH "garbled maps" were the SAME bug: BACKBUF-RECOVER
             * swapping the empty/sparse displayed buffer for the fuller compose master (0x80000 = the
             * stale overland map) -- once for the bg8.piv win screen, once for the relaunch.  Gating
             * BACKBUF-RECOVER off during the delivery (render_rgb, g_delivery_win==0) stops it at the
             * source, so neither screen garbles now: verified the win screen AND the "ceremony of the
             * Moonstone / Loading" relaunch screen both render clean, blackout never fires (2026-06-27). */
        }
        if (!g_rawcapture && palette_distinct_nonzero() <= 3 &&
            frame_ink_fraction(g_cap_tmp,h) < TRANSITION_HOLD_INK &&
            g_hold_run < TRANSITION_HOLD_MAX &&
            !(g_autoswap_armed && r32(0x2fb1cu) == 9u)) {  /* EXCEPT the disk-insert prompt ("Please insert
                                          * Disk A") -- it's a legit screen that is near-empty + few colours,
                                          * so it otherwise looks like mid-clear garbage and gets held black.
                                          * While the swap is armed (delivery), show it instead of holding. */
            g_hold_run++;                 /* hold: keep the previous g_cap as-is */
            if (dlog) { static int n=0; if (n++<600) fprintf(g_log, "CAP T scene=%u garb=%d pdiff=%d coh=%.2f tfill=%d win=%d\n", dsc, g_render_garbled, g_dbg_paldiff, g_dbg_coh, tfill, g_delivery_win); }
            if (g_rdbg && g_log)
                fprintf(g_log, "TRANSITION-HOLD fr=%d run=%d (degenerate palette + mid-clear, holding last frame)\n",
                        g_cur_frame, g_hold_run);
            return;
        }
        /* GIVE-CUTSCENE garble suppression: render_rgb flagged the displayed give-ritual buffer as a
         * mid-composite garble -> hold the last good frame (the black fade) so the garbage is never
         * shown, until the real cutscene composites (coherent -> flag clears) or the cap expires. */
        if (g_render_garbled && g_garble_hold < (g_render_garbled==2 ? GARBLE_HOLD_DELIVERY : GARBLE_HOLD_MAX)) {
            g_garble_hold++;
            if (g_render_garbled == 2) {
                /* DELIVERY: BLACK OUT rather than hold the last frame.  The map palette is clobbered
                 * BEFORE the latch arms (0x22820), so the last captured frame is itself the garbled map
                 * -- holding it just freezes the garble (the live bug: garb=2 every frame, hold firing,
                 * but the held frame IS the garbled map).  A forced black is clean and is what the
                 * operator prefers for this whole delivery window. */
                memset(g_cap, 0, sizeof(g_cap));
                g_cap_w = w; g_cap_h = h; g_cap_valid = 1;
            }
            if (dlog) { static int n=0; if (n++<600) fprintf(g_log, "CAP %c scene=%u garb=%d pdiff=%d coh=%.2f tfill=%d win=%d\n", g_render_garbled==2?'B':'H', dsc, g_render_garbled, g_dbg_paldiff, g_dbg_coh, tfill, g_delivery_win); }
            if (g_rdbg && g_log)
                fprintf(g_log, "GARBLE-HOLD fr=%d run=%d mode=%d (Stonehenge transition garbled, %s)\n",
                        g_cur_frame, g_garble_hold, g_render_garbled,
                        g_render_garbled==2 ? "blacked out" : "holding last frame");
            return;
        }
        if (dlog) { static int n=0; if (n++<600) fprintf(g_log, "CAP N scene=%u garb=%d pdiff=%d tfill=%d disp=%06x win=%d\n", dsc, g_render_garbled, g_dbg_paldiff, tfill, g_disp_base, g_delivery_win); }
        g_hold_run = 0; g_garble_hold = 0;
        memcpy(g_cap, g_cap_tmp, sizeof(g_cap));
        g_cap_w = w; g_cap_h = h; g_cap_valid = 1;
        return;
    }
    memset(g_cap,0,sizeof(g_cap));
    render_rgb(g_cap,&w,&h);
    g_firewait_hot = 0; g_mappoll_hot = 0;
    g_cap_w = w; g_cap_h = h; g_cap_valid = 1;
}

static int write_ppm(const char *path, const uint8_t *fb, int w, int h) {
    FILE *f=fopen(path,"wb"); if(!f) return -1;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for (int y=0;y<h;y++) fwrite(fb+y*FB_W*3,3,w,f);
    fclose(f);
    return 0;
}

static int dump_ppm(const char *path) {
    int w,h; memset(g_fb,0,sizeof(g_fb));
    render_rgb(g_fb,&w,&h);
    return write_ppm(path, g_fb, w, h);
}

/* Write the last vblank-aligned capture (falls back to a fresh render). */
static int dump_captured(const char *path) {
    if (!g_cap_valid) return dump_ppm(path);
    return write_ppm(path, g_cap, g_cap_w, g_cap_h);
}

/* --------------------------------------------------------- frame stepping */
#define CYCLES_PER_FRAME 141876   /* PAL 7.09 MHz / 50 Hz */

static void autoswap_tick(void);   /* fwd: seamless disk-swap auto-confirm (defined below) */
static void apply_script(int fr);  /* fwd: scripted input (allows --script under --sdl for repro) */
static int  g_nscript;             /* fwd (tentative): script event count, defined with parse_script below */
static int  save_state(const char *path);   /* fwd: quicksave  (F5; defined near main) */
static int  load_state(const char *path);   /* fwd: quickload  (F9; defined near main) */
static int  load_in_progress(void);         /* fwd: 1 while a disk/file load is mid-flight (defined below) */
static void run_one_frame(void) {
    g_frame_cycle = 0;
    int zeroburst = 0;
    while (g_frame_cycle < CYCLES_PER_FRAME && !g_stop) {
        update_ipl();
        int did = m68k_execute(160);     /* small bursts: ~0.35 raster lines */
        /* If the 68k double-bus-faults (a runaway smashed A7 -> the exception push itself
         * faults), Musashi HALTS the CPU and m68k_execute returns 0 forever.  Without this,
         * the frame loop spins here in host code and LOCKS UP THE WINDOW (operator couldn't
         * even close it, 2026-06-21).  After a few zero-progress bursts, stop gracefully so
         * run_sdl reports + exits instead of hanging.  (Normal STOP-wait idles >0 cycles.) */
        if (did <= 0) { if (++zeroburst >= 8) { halt("cpu halted (double bus fault / runaway)"); break; } }
        else zeroburst = 0;
        /* Uniform DMA cycle-steal: advance the frame/beam/CIA clock by did+steal
         * while the CPU only executed `did`, so the frame completes after fewer
         * CPU instructions (see g_dma_steal_pct). Attract-scoped only. */
        int elapsed = did;
        if (g_blt_busy_scope && g_dma_steal_pct > 0)
            elapsed += did * g_dma_steal_pct / 100;
        g_frame_cycle += elapsed; g_cycles += elapsed;
        beam_update(); cia_tick(elapsed);
        /* Generate Paula audio in step with the CPU so a one-shot SFX's AUDx
         * level-4 IRQ (raised here at buffer-end) is serviced by the NEXT burst
         * -- the game's sound-server ISR repoints the channel to silence before
         * the sample can replay (faithful; see audio_advance). */
        audio_advance((int)((int64_t)SMP_PER_FRAME * g_frame_cycle / CYCLES_PER_FRAME));
    }
    if (vertb_gate()) { g_custom[0x09c>>1] |= 0x0020; }  /* VERTB */
    update_ipl();
    audio_flush();   /* finish + emit one PAL frame of Paula audio to the host sink */
}

#ifdef _WIN32
/* Last-resort crash reporter: if the HOST process faults (e.g. the chipset model
 * dereferences out of bounds) the game would otherwise vanish with no trace
 * ("poof").  Catch the unhandled exception, write the 68k state to the log, and
 * show a message box pointing at it, then terminate. Covers the class of crashes
 * that halt() can't see (a halt is a clean stop; this is a real segfault). */
static LONG WINAPI moon_crash_filter(EXCEPTION_POINTERS *ep) {
    unsigned long code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
    unsigned long long fault_addr = 0; int fault_rw = -1;
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->NumberParameters >= 2) {
        fault_rw   = (int)ep->ExceptionRecord->ExceptionInformation[0]; /* 0=read 1=write 8=DEP */
        fault_addr = (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1];
    }
    if (g_log && g_log != stderr) {
        fprintf(g_log, "--- FATAL host exception 0x%08lx  68k-pc=%06x icount=%llu unmapped=%u "
                "fault=%s addr=%016llx ---\n",
                code, pc, (unsigned long long)g_icount, g_unmapped,
                fault_rw==1?"write":fault_rw==0?"read":"?", fault_addr);
        fflush(g_log);
    }
    char msg[512];
    snprintf(msg, sizeof(msg),
             "Moonstone hit a fatal internal error.\n\n"
             "code=0x%08lx  68k-pc=%06x  icount=%llu\n\n"
             "A log was written next to the game:\n%s\\moonstone.log\n\n"
             "Please send that file so this can be fixed.",
             code, pc, (unsigned long long)g_icount, g_exedir[0] ? g_exedir : ".");
    if (g_sdl_mode)   /* only pop a dialog in live play; headless tests just log */
        MessageBoxA(NULL, msg, "Moonstone \xe2\x80\x94 crash", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;  /* terminate */
}
#endif

/* Minimal base64 decoder for the embedded boot splash (output-only asset).
 * Skips any non-base64 chars (newlines etc.); '=' padding is a no-op. */
static inline int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static int b64decode(const char *s, uint8_t *out, int cap) {
    int acc = 0, bits = 0, n = 0;
    for (; *s; s++) {
        int v = b64val((unsigned char)*s);
        if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; if (n < cap) out[n++] = (uint8_t)((acc >> bits) & 0xff); }
    }
    return n;
}

/* ----------------------------------------------------------- SDL host loop */
static int run_sdl(int scale) {
    g_sdl_mode = 1;   /* live play: crash reporter may show a dialog box */
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    /* open Paula host audio: 44100 Hz, signed-16 stereo, push ~882 frames/frame */
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = HOST_RATE; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = 1024; want.callback = NULL;   /* push model via SDL_QueueAudio */
    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio_dev) {
        g_audio_on = 1;
        if (g_log) { fprintf(g_log, "AUDIO-DEV want{rate=%d fmt=%04x ch=%d samp=%d} have{rate=%d fmt=%04x ch=%d samp=%d sz=%d}\n",
            want.freq, want.format, want.channels, want.samples,
            have.freq, have.format, have.channels, have.samples, have.size); fflush(g_log); }
        /* Prime the queue with silence so the device has a cushion before the first
         * generated frame lands.  Without a cushion any single slow host frame
         * drains the queue to empty and the DAC repeats / clicks -- the "messy"
         * intro audio.
         *
         * For the attract intro we prime a DEEP cushion (g_av_delay_frames frames)
         * so the audio runs ~N frames behind generation, lining the lightning SFX
         * up with the lagged visual (the audio output delay -- see g_av_delay_frames).
         * The audio_flush() cap keeps that backlog steady during the attract; at Mog
         * launch the cap drops and the backlog is drained back to minimal latency.
         * If --avdelay is 0 (or not in the attract) we fall back to a small ~2-frame
         * cushion = minimal latency. */
        int prime_frames = (g_blt_busy_scope && g_av_delay_frames > 0) ? g_av_delay_frames
                         : 4;                      /* cushion vs host-frame jitter (was 2 -> too shallow, starved) */
        g_av_attract_audio = (g_blt_busy_scope && g_av_delay_frames > 0);
        const Uint32 frame_bytes = (Uint32)(SMP_PER_FRAME*2*sizeof(int16_t));
        int16_t *prime = (int16_t *)calloc((size_t)prime_frames * SMP_PER_FRAME * 2, sizeof(int16_t));
        if (prime) {
            /* AQ-PRIME diag (cushion-loss hunt 2026-07-02): the shipped log showed q=0.00 at
             * AUDIO-UNPAUSE, i.e. this prime was gone by the end of the load.  Log the queue
             * call's outcome + the actual device queue depth so a failed queue can be told
             * apart from a device that consumes while nominally paused. */
            int rc = SDL_QueueAudio(g_audio_dev, prime, frame_bytes * (Uint32)prime_frames);
            free(prime);
            if (g_log) { fprintf(g_log, "AQ-PRIME frames=%d rc=%d err=%s q=%u bytes\n",
                                 prime_frames, rc, rc ? SDL_GetError() : "-",
                                 (unsigned)SDL_GetQueuedAudioSize(g_audio_dev)); fflush(g_log); }
        }
        /* Start PAUSED: audio_flush unpauses on the first audible frame so the prime
         * cushion survives the disk-load instead of draining to empty (the intro tick). */
        g_audio_paused = 1;
    }
    else fprintf(stderr, "SDL_OpenAudioDevice: %s (continuing silent)\n", SDL_GetError());
    snprintf(g_wintitle, sizeof(g_wintitle), "Moonstone (native) - build %s", MOON_BUILD);
    SDL_Window  *win = SDL_CreateWindow(g_wintitle, SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 320*scale, 256*scale, SDL_WINDOW_RESIZABLE);
    /* Window/taskbar/alt-tab icon = the credit-scene moon (the embedded .ico only
     * covers the Explorer FILE icon; the running window needs SDL_SetWindowIcon). */
    if (win) {
        SDL_Surface *ic = SDL_CreateRGBSurfaceWithFormatFrom(
            (void*)moon_icon_rgba, MOON_ICON_W, MOON_ICON_H, 32, MOON_ICON_W*4, SDL_PIXELFORMAT_RGBA32);
        if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); }
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    SDL_RenderSetLogicalSize(ren, 320, 256);
    /* Boot splash: decode the embedded RLE image once into a static texture.  Shown
     * during the (black) disk-load until the intro composes its first frame -- see the
     * present block below.  Output-only; never touches game state. */
    SDL_Texture *splash_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, SPLASH_W, SPLASH_H);
    if (splash_tex) {
        SDL_SetTextureScaleMode(splash_tex, SDL_ScaleModeLinear);  /* smooth if the window is resized off 1:1 */
        int b64len = (int)strlen(splash_b64), cap = SPLASH_W*SPLASH_H*3;
        uint8_t *qoi = (uint8_t*)malloc((size_t)b64len);           /* >= decoded QOI size */
        uint8_t *sb  = (uint8_t*)malloc((size_t)cap);              /* RGB24 pixels */
        if (qoi && sb) {
            int qn = b64decode(splash_b64, qoi, b64len);           /* base64 -> QOI bytes */
            if (qoi_decode_rgb(qoi, qn, sb, cap) == cap)           /* QOI -> RGB24 */
                SDL_UpdateTexture(splash_tex, NULL, sb, SPLASH_W*3);
        }
        free(qoi); free(sb);
    }
    int boot_splash_done = 0;
    /* open the first attached game controller (hot-plug handled in the loop) */
    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); if (pad) break; }
    fprintf(stderr, "gamepad: %s\n", pad ? SDL_GameControllerName(pad) : "none (keyboard/mouse only)");

    /* keyboard + mouse-button state is held here and OR'd with the pad each
     * frame, so neither input source clobbers the other. */
    int kb_u=0, kb_d=0, kb_l=0, kb_r=0, kb_fire=0;
    int m_fire=0, m_rmb=0;
    int running = 1;
    int status_frames = 0;   /* >0: a SAVED/LOADED title flash is up; restore g_wintitle at 0 */
    char savepath[1100];
    snprintf(savepath, sizeof(savepath), "%s/moonstone.sav", g_exedir[0] ? g_exedir : ".");
    /* High-resolution frame pacing.  PAL is 50 Hz, so each frame should occupy
     * exactly 1/50 s of wall time.  SDL_GetTicks()/SDL_Delay() are millisecond-
     * resolution and SDL_Delay over-sleeps by the OS timer granularity (up to
     * ~15 ms on a Windows box without a raised timer), which on the LIGHT intro
     * frames (a fraction of a ms of real work) inflates each frame well past
     * 20 ms -> the intro plays slow AND we push < 44100 audio samples/sec so the
     * audio device starves and crackles.  (In-game frames do more host work, so
     * less padding is requested and the over-sleep is proportionally smaller --
     * which is exactly why the intro was slow but combat sounded fine.)  Pace
     * against the performance counter with a fractional deadline that never
     * drifts, sleeping coarsely then spinning the last ~2 ms for accuracy. */
    const Uint64 pf = SDL_GetPerformanceFrequency();
    const double frame_ticks = (double)pf / 50.0;     /* perf-counter ticks / PAL frame */
    double next_deadline = (double)SDL_GetPerformanceCounter() + frame_ticks;

    /* Negative --avdelay: hold the DISPLAY back |N| frames during the attract so the
     * audio LEADS the picture (see g_av_delay_frames).  Ring of captured RGB24 frames;
     * each frame we store the fresh capture and present the one from |N| frames ago
     * (the first frame is held until the ring fills).  Snapped to live at Mog launch. */
    g_av_attract_video = (g_blt_busy_scope && g_av_delay_frames < 0);
    int      vdelay = (g_av_delay_frames < 0) ? -g_av_delay_frames : 0;
    int      vcap   = vdelay + 1;                 /* ring slots: |N| back + the current frame */
    uint8_t *vring  = NULL;
    int     *vrw    = NULL, *vrh = NULL;
    long     vframe = 0;
    if (g_av_attract_video && vdelay > 0) {
        vring = (uint8_t *)malloc((size_t)vcap * FB_W * FB_H * 3);
        vrw   = (int *)malloc((size_t)vcap * sizeof(int));
        vrh   = (int *)malloc((size_t)vcap * sizeof(int));
        if (!vring || !vrw || !vrh) {             /* OOM: fall back to live video */
            free(vring); free(vrw); free(vrh);
            vring = NULL; vrw = vrh = NULL; g_av_attract_video = 0;
        }
    }
    int skip_intro = 0;   /* set by any key/pad during the attract -> fast-forward to Mog launch */
    while (running && !g_stop) {
        g_cur_frame++;   /* advance the host-frame counter in the LIVE path too: it was only set in the
                          * headless --frames loop, so in real play g_cur_frame stayed 0 and the 3 s
                          * win-screen auto-advance timer (el = g_cur_frame - g_winwait_frame) never
                          * elapsed.  A monotonic per-frame tick is all that timer + the maplog need. */
        int do_save = 0, do_load = 0;   /* quicksave/quickload requested this frame (F5/F9) */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int d = (e.type == SDL_KEYDOWN);
                int sym = e.key.keysym.sym;
                /* TYPED-TEXT: on keydown queue the 0xc1b3-index for any text key
                 * (A-Z, 0-9, space, Backspace, Return) so the Select-Knight name
                 * field can be typed into.  The queue is only drained at the name-
                 * entry poll site, so this is inert on every other screen.  Done in
                 * addition to (not instead of) the navigation mapping below, so the
                 * same keys still drive fire/menu where those screens are active. */
                if (d) { uint8_t ix = keysym_to_idx(sym); if (ix) keyq_push(ix); }
                switch (sym) {
                    case SDLK_ESCAPE: if (d) running = 0; break;
                    case SDLK_F5: if (d) do_save = 1; break;          /* quicksave (acts at frame end) */
                    case SDLK_F9: if (d) do_load = 1; break;          /* quickload (acts at frame end) */
                    case SDLK_F10: if (d) { g_livedump = !g_livedump;  /* toggle live frame recorder (diagnostics) */
                        if (win) SDL_SetWindowTitle(win, g_livedump ? "Moonstone - RECORDING FRAMES (F10 to stop)" : g_wintitle); } break;
                    case SDLK_F12: if (d) toggle_record(win); break;  /* toggle audio->WAV capture (capture-N.wav next to exe + reg-log into moonstone.log); re-bound 2026-06-25 for the combat-screech hunt */
                    /* F7/F8 key bindings removed (operator).  The underlying diagnostics stay:
                     * dump_derail() still auto-fires on a derail/wild-PC/A7-smash; the task-list
                     * fix (g_tasklist_fix, default ON, --notaskfix to A/B headless) remains -- just
                     * no longer bound to F-keys.  (F12 re-bound to audio capture, above.) */
                    case SDLK_UP:    kb_u = d; break;
                    case SDLK_DOWN:  kb_d = d; break;
                    case SDLK_LEFT:  kb_l = d; break;
                    case SDLK_RIGHT: kb_r = d; break;
                    case SDLK_LCTRL: case SDLK_RCTRL:
                    case SDLK_RETURN: kb_fire = d; break;
                    /* Space = open INVENTORY on the map (the faithful Amiga control);
                     * fire stays on Ctrl/Enter/mouse/pad.  I is a backup.  Gated to
                     * in-game so Space still SKIPS the attract intro. */
                    case SDLK_SPACE: case SDLK_i:
                        if (d && !g_blt_busy_scope && !g_in_inventory) g_inv_request = 1; break;
                    /* E = REST / skip to the next turn on the overland map (the faithful Amiga
                     * key).  Edge-only (keydown), in-game; inert during the attract/inventory. */
                    case SDLK_e:
                        if (d && !g_blt_busy_scope && !g_in_inventory) g_rest_request = 1; break;
                    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5:
                    case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9:
                        g_kdigit = d ? (e.key.keysym.sym - SDLK_1 + 1) : 0; break;  /* overlap-popup select */
                    default: break;
                }
                /* Any key during the attract intro = skip it (fast-forward to the
                 * game start).  WHITELIST the deliberate FIRE/confirm keys only --
                 * Space, Enter (main or numpad) and Ctrl -- so a stray key such as a
                 * media/VOLUME key (or the F5/F8/F9 save/capture keys) no longer skips
                 * the intro by accident. */
                if (d && g_blt_busy_scope &&
                    (sym == SDLK_SPACE || sym == SDLK_RETURN || sym == SDLK_KP_ENTER ||
                     sym == SDLK_LCTRL || sym == SDLK_RCTRL))
                    skip_intro = 1;
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                int d = (e.type == SDL_MOUSEBUTTONDOWN);
                if (e.button.button == SDL_BUTTON_LEFT)   m_fire = d;
                if (e.button.button == SDL_BUTTON_RIGHT)  m_rmb = d;
                /* NOTE: a mouse click no longer skips the intro -- the window isn't
                 * always focused, so a click to (re)focus it shouldn't skip.  Skip is
                 * Space/Enter/Ctrl or a controller button only. */
            } else if (e.type == SDL_MOUSEMOTION) {
                /* feed relative motion to the JOY0DAT mouse counter so the menu
                 * cursor tracks the real mouse (clamp the per-frame delta so a
                 * fast flick can't wrap the 8-bit counter). */
                int dx = e.motion.xrel, dy = e.motion.yrel;
                if (dx >  8) dx =  8; else if (dx < -8) dx = -8;
                if (dy >  8) dy =  8; else if (dy < -8) dy = -8;
                g_mouse_dx += dx; g_mouse_dy += dy;
            } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (!pad && SDL_IsGameController(e.cdevice.which)) {
                    pad = SDL_GameControllerOpen(e.cdevice.which);
                    fprintf(stderr, "gamepad connected: %s\n", pad ? SDL_GameControllerName(pad) : "?");
                }
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (pad) { SDL_GameControllerClose(pad); pad = NULL; }
            }
        }

        /* poll the game controller: D-pad / left stick drive BOTH the digital
         * joystick (JOY1DAT: combat + overland + main menu) AND the mouse cursor
         * (JOY0DAT: equip/altar + cursor menus), so one pad works everywhere. */
        int pad_u=0, pad_d=0, pad_l=0, pad_r=0, pad_fire=0, pad_rmb=0, pad_mx=0, pad_my=0;
        if (pad) {
            const int DZ = 12000;
            int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            int dl = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            int dr = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            int du = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
            int dd = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            pad_l = dl || lx < -DZ;  pad_r = dr || lx > DZ;
            pad_u = du || ly < -DZ;  pad_d = dd || ly > DZ;
            pad_fire = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)
                     || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                     || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                     || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
            pad_rmb = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
            /* Y / Start = open INVENTORY on the map (edge-detected, in-game only). */
            {
                int inv_btn = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y)
                           || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
                static int prev_inv_btn = 0;
                if (inv_btn && !prev_inv_btn && !g_blt_busy_scope && !g_in_inventory) g_inv_request = 1;
                prev_inv_btn = inv_btn;
            }
            /* Back = REST / skip to the next turn on the overland map (edge-detected, in-game
             * only) — mirrors keyboard 'E'.  Quit stays on Esc / the window close box only. */
            {
                int rest_btn = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
                static int prev_rest_btn = 0;
                if (rest_btn && !prev_rest_btn && !g_blt_busy_scope && !g_in_inventory) g_rest_request = 1;
                prev_rest_btn = rest_btn;
            }
            /* analog cursor speed for mouse-driven menus (clamped ±8/frame) */
            if (lx > DZ || lx < -DZ) { pad_mx = lx / 3200; if (pad_mx>8) pad_mx=8; if (pad_mx<-8) pad_mx=-8; }
            else pad_mx = (dr?6:0) - (dl?6:0);
            if (ly > DZ || ly < -DZ) { pad_my = ly / 3200; if (pad_my>8) pad_my=8; if (pad_my<-8) pad_my=-8; }
            else pad_my = (dd?6:0) - (du?6:0);
        }
        if (g_blt_busy_scope && (pad_fire|pad_rmb))
            skip_intro = 1;   /* a controller BUTTON (A/B/RB/RT/Start) skips the intro
                               * (not stick/d-pad nudges, so drift can't skip it) */
        /* --skipat N (diag, cushion-loss repro 2026-07-02): trigger the intro-skip
         * automatically at host frame N, so the post-skip audio-queue state can be
         * reproduced/verified headlessly (no manual keypress).  0 = off. */
        if (g_skipat && g_blt_busy_scope && g_cur_frame >= g_skipat)
            skip_intro = 1;

        /* combine keyboard + mouse-button + pad into the chipset input globals */
        g_ji_up = kb_u | pad_u;  g_ji_dn = kb_d | pad_d;
        g_ji_lf = kb_l | pad_l;  g_ji_rt = kb_r | pad_r;
        g_fire  = g_fire2 = kb_fire | m_fire | pad_fire;
        g_rmb   = m_rmb | pad_rmb;
        g_mouse_dx += pad_mx;  g_mouse_dy += pad_my;

        if (skip_intro && g_blt_busy_scope) {
            /* SKIP THE INTRO: the attract is a scripted auto-flow that launches the
             * Mog game engine at 0x21114 (which clears g_blt_busy_scope).  Run the
             * emulation unthrottled with no render/audio until that point, then resume
             * normal play.  Bypass the video-delay ring (its buffered frames are stale
             * after a fast-forward) and drop any queued intro audio at the end. */
            g_av_attract_video = 0; g_av_drain = 0;
            if (win) SDL_SetWindowTitle(win, "Moonstone - skipping intro...");
            /* MUTE audio for the duration of the fast-forward: otherwise audio_flush
             * keeps queueing frames, and because the FF runs unthrottled the 8-frame
             * queue cap SAMPLES one frame from far-apart points in the rapidly-advancing
             * intro -> the device plays a ~100x time-compressed garble.  Drop whatever
             * is already queued and stop pushing; the empty queue plays clean silence. */
            if (g_audio_dev) SDL_ClearQueuedAudio(g_audio_dev);
            g_audio_mute = 1;
            int guard = 0;
            while (g_blt_busy_scope && !g_stop && guard < 20000) {
                g_ji_up=g_ji_dn=g_ji_lf=g_ji_rt=0; g_fire=g_fire2=0; g_rmb=0;
                autoswap_tick();       /* keep auto-confirming disk-swap prompts */
                run_one_frame();
                if ((++guard % 600) == 0) SDL_PumpEvents();   /* keep the window responsive */
            }
            if (win) SDL_SetWindowTitle(win, g_wintitle);
            skip_intro = 0;
            g_av_attract_video = 0; g_av_drain = 0;   /* stay live post-skip (no tail drain) */
            g_audio_mute = 0;                              /* resume audio for live play */
            if (g_audio_dev) SDL_ClearQueuedAudio(g_audio_dev);
            audio_reprime(4);   /* rebuild the anti-underrun cushion the clear just destroyed */
            next_deadline = (double)SDL_GetPerformanceCounter() + frame_ticks;  /* resync pacing */
            continue;          /* next iteration renders the live post-intro frame */
        }

        /* QUICKSAVE (F5) / QUICKLOAD (F9): act at this clean between-frames boundary,
         * in-game only (saving/loading during the scripted attract is meaningless and
         * would fight the video-delay ring).  Whole-machine snapshot to/from
         * <exedir>/moonstone.sav; a 2.4s title flash gives feedback. */
        if ((do_save || do_load) && !g_blt_busy_scope) {
            const char *msg;
            if (do_save) {
                msg = load_in_progress() ? "BUSY LOADING - SAVE IN A SEC"
                    : save_state(savepath) ? "GAME SAVED (F5)" : "SAVE FAILED";
            } else {
                if (load_state(savepath)) {
                    msg = "GAME LOADED (F9)";
                    if (g_audio_dev) SDL_ClearQueuedAudio(g_audio_dev);   /* drop now-stale queued audio */
                    audio_reprime(4);   /* rebuild the anti-underrun cushion the clear just destroyed */
                    next_deadline = (double)SDL_GetPerformanceCounter() + frame_ticks;  /* resync pacing */
                } else {
                    msg = "NO SAVE TO LOAD";
                }
            }
            if (win) {
                char t[256]; snprintf(t, sizeof(t), "%s   <<< %s >>>", g_wintitle, msg);
                SDL_SetWindowTitle(win, t);
                status_frames = 120;   /* ~2.4s, then restore the base title */
            }
        }

        Uint64 t_w0 = SDL_GetPerformanceCounter();   /* frame-time watch: start of host work */
        { static int sdlfr = 0; if (g_nscript) apply_script(sdlfr); sdlfr++; }  /* allow --script under --sdl (dummy-video repro) */
        autoswap_tick();   /* seamless disk swap: auto-confirm any "insert disk" prompt */
        run_one_frame();
        g_mouse_dx = g_mouse_dy = 0;   /* consume this frame's mouse delta */
        /* Render via capture_frame so the live window gets the same vblank-aligned
         * snapshot + empty-backbuffer recovery + scene-transition hold as the dump
         * path (clean scene switches, no half-decoded frames). */
        capture_frame();
        int w = g_cap_w, h = g_cap_h;
        const uint8_t *disp = g_cap;
        if (g_av_attract_video && vring && !g_av_drain) {
            /* attract: store this frame, present the one captured |vdelay| frames ago */
            int slot = (int)(vframe % vcap);
            memcpy(vring + (size_t)slot*FB_W*FB_H*3, g_cap, (size_t)FB_W*FB_H*3);
            vrw[slot] = g_cap_w; vrh[slot] = g_cap_h;
            /* LEGEND RAMP: once the legend scene begins (g_av_ramp, after all thunder),
             * walk the delay down to 0 so the picture is caught up to live by Mog launch
             * and the -2.5s tail adds no black at the intro->gameplay hand-off.  The ring
             * still buffers fresh frames; we only shrink how far back we present, so the
             * legend crawl plays slightly faster (no audio sync to disturb past thunder). */
            if (g_av_ramp && vdelay > 0) vdelay--;
            long tgt = vframe - vdelay;
            int dslot = (tgt >= 0) ? (int)(tgt % vcap) : 0;  /* hold first frame until the ring fills */
            disp = vring + (size_t)dslot*FB_W*FB_H*3;
            w = vrw[dslot]; h = vrh[dslot];
            vframe++;
            if (g_av_ramp && vdelay == 0) { g_av_attract_video = 0; }  /* caught up: live for the rest */
        } else if (g_av_attract_video && vring && g_av_drain) {
            /* Mog launched: drain the buffered intro tail at natural speed over the
             * (black) loader, then snap to live.  Freeze the ring (stop storing the
             * loader's blank frames) and shrink the delay by 1/frame so the display
             * walks forward to the newest buffered frame (vframe-1), then go live --
             * the load is hidden behind the real end of the intro instead of black. */
            if (vdelay > 0) vdelay--;
            long tgt = vframe - 1 - vdelay;
            if (tgt < 0) tgt = 0;
            int dslot = (int)(tgt % vcap);
            disp = vring + (size_t)dslot*FB_W*FB_H*3;
            w = vrw[dslot]; h = vrh[dslot];
            if (vdelay == 0) { g_av_attract_video = 0; g_av_drain = 0; }  /* fully caught up: live */
        }
        if (g_delivery_win > 0 && g_log) {   /* PRESENT diag: what is actually sent to the texture vs g_cap */
            static int n=0; if (n++<300) {
                long ds=0, cs=0; for (int k=0;k<w*3;k++){ ds+=disp[k]; cs+=g_cap[k]; }
                fprintf(g_log, "PRESENT fr-row0sum disp=%ld cap=%ld eq=%d attract=%d\n",
                        ds, cs, disp==g_cap, g_av_attract_video);
            }
        }
        /* Boot splash: until the DISPLAYED frame first carries real content (disk load
         * done + intro composed; this also rides out the attract video-delay so there's
         * no black gap after the splash), show the splash instead of black.  One-way:
         * once content appears the splash is never shown again, so the game is untouched. */
        if (!boot_splash_done) {
            long nz = 0;
            for (int k = 0; k < FB_W*FB_H*3; k++) if (disp[k]) { if (++nz >= SPLASH_BOOT_NZ) break; }
            if (nz >= SPLASH_BOOT_NZ) boot_splash_done = 1;
        }
        if (!boot_splash_done && splash_tex) {
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, splash_tex, NULL, NULL);
            SDL_RenderPresent(ren);
        } else {
            SDL_UpdateTexture(tex, NULL, disp, FB_W*3);
            SDL_Rect srcr = {0, 0, w, h};
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, &srcr, NULL);
            SDL_RenderPresent(ren);
        }
        if (g_livedump) {   /* diagnostics: record each displayed frame next to the exe (F10 toggles) */
            static int ld_n = 0;
            static FILE *ldlog = NULL;
            if (!ldlog) {       /* sidecar chipset-state log (separate file; never touches moonstone.log) */
                char lp[1280];
                snprintf(lp, sizeof(lp), "%s%slivedump.log", g_exedir[0]?g_exedir:".", g_exedir[0]?"/":"");
                ldlog = fopen(lp, "w");
            }
            if (ld_n < 1200) {   /* safety cap (~24s) so a forgotten toggle can't fill the disk */
                char p[1280];
                snprintf(p, sizeof(p), "%s%slivedump_%04d.ppm",
                         g_exedir[0] ? g_exedir : ".", g_exedir[0] ? "/" : "", ld_n);
                write_ppm(p, disp, w, h);
                if (ldlog) {     /* per-frame: scene, palette richness, ink, load, + render_rgb display source */
                    fprintf(ldlog, "fr=%d scene=%u pal=%d ink=%d load=%d cp=%06x stride=%05x nplanes=%d dispbase=%06x recover=%06x\n",
                            ld_n, (unsigned)r32(0x2fb1c),
                            palette_distinct_nonzero(), (int)(frame_ink_fraction(disp,h)*100.0),
                            load_in_progress(), g_dbg_cp, g_dbg_stride, g_dbg_nplanes, g_dbg_dispbase, g_dbg_recover);
                    fflush(ldlog);
                }
                ld_n++;
            }
        }
        if (status_frames > 0 && --status_frames == 0 && win)
            SDL_SetWindowTitle(win, g_wintitle);   /* SAVED/LOADED flash expired -> restore title */
        /* ALWAYS-ON FRAME-TIME WATCH (in-game only): how long the host took to make
         * this frame (emulate CPU + render), BEFORE the pacing sleep.  >20ms means the
         * host can't hit 50fps = a real perf bug in this port; staying low while the
         * action still drags = faithful CPU-bound slowdown from the original game. */
        if (g_os && !g_blt_busy_scope) {
            static int    fs_n = 0, fs_over = 0, slow_logged = 0;
            static double fs_sum = 0.0, fs_max = 0.0;
            double work_ms = ((double)SDL_GetPerformanceCounter() - (double)t_w0) * 1000.0 / (double)pf;
            fs_n++; fs_sum += work_ms;
            if (work_ms > fs_max) fs_max = work_ms;
            if (work_ms > 20.0) fs_over++;
            if (work_ms > 33.0 && g_log && slow_logged < 300) {   /* hitch: missed >=1 extra frame */
                fprintf(g_log, "SLOWFRAME work_ms=%.1f ic=%llu\n", work_ms, (unsigned long long)g_icount);
                fflush(g_log); slow_logged++;
            }
            if (fs_n >= 250) {                                    /* ~5s heartbeat */
                if (g_log) {
                    fprintf(g_log, "FRAMESTAT n=%d avg_ms=%.2f max_ms=%.1f over20=%d ic=%llu\n",
                            fs_n, fs_sum/(double)fs_n, fs_max, fs_over, (unsigned long long)g_icount);
                    fflush(g_log);
                }
                fs_n = 0; fs_sum = 0.0; fs_max = 0.0; fs_over = 0;
            }
        }
        /* --stallat N (diag): simulate a >4-frame host stall (a window drag / OS
         * hitch) at frame N, one-shot -- the repro vehicle for the stall-resync
         * cushion top-up below.  The device keeps consuming during the sleep. */
        if (g_stallat && g_cur_frame == g_stallat) SDL_Delay(500);
        /* Wait until this frame's high-resolution deadline: sleep most of the
         * remaining time (cheap), then spin the final couple ms (accurate). */
        for (;;) {
            double now = (double)SDL_GetPerformanceCounter();
            double remain_ms = (next_deadline - now) * 1000.0 / (double)pf;
            if (remain_ms <= 0.2) break;
            if (remain_ms > 3.0) SDL_Delay((Uint32)(remain_ms - 2.0));
            /* else busy-spin the tail for sub-ms accuracy */
        }
        next_deadline += frame_ticks;
        /* If we fell badly behind (e.g. window drag / OS stall), resync the
         * deadline to now so we don't sprint to "catch up" and desync audio. */
        {
            double now = (double)SDL_GetPerformanceCounter();
            if (now - next_deadline > frame_ticks * 4.0) {
                next_deadline = now + frame_ticks;
                /* The stall just let the (still-running) device drain the queue in
                 * real time, and since we deliberately don't sprint, nothing would
                 * regenerate that cushion (generation == playback; the flush cap only
                 * LIMITS pushes) -- the fourth and last path into the chronic
                 * <1-frame starvation state the clear-site audio_reprime calls fixed.
                 * Top the cushion back up to the boot prime depth.  Scoped to the
                 * stall-resync ONLY: a per-frame top-up would inject audible silence
                 * gaps on ordinary clock drift, whereas here the device has already
                 * gapped during the stall, so the inserted silence costs nothing.
                 * Sub-4-frame stalls self-heal via the catch-up sprint (each caught-up
                 * frame pushes a queue frame) and never reach this branch.  Paused
                 * (load) and deep-backlog (attract --avdelay) states are no-ops via
                 * the shortfall math. */
                if (g_audio_dev && !g_audio_paused && !g_audio_mute) {
                    const Uint32 fb = (Uint32)(SMP_PER_FRAME*2*sizeof(int16_t));
                    Uint32 q = SDL_GetQueuedAudioSize(g_audio_dev);
                    if (q < 4u*fb) audio_reprime((int)((4u*fb - q + fb - 1u) / fb));
                }
            }
        }
    }
    free(vring); free(vrw); free(vrh);
    /* If we exited because the core HALTED (not a normal Esc/window-close), report
     * WHY — write it to the log and show a message box, instead of vanishing. */
    if (g_stop) {
        unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
        if (g_log) {
            fprintf(g_log, "--- EXIT (sdl): stop=%s pc=%06x icount=%llu unmapped=%u ---\n",
                    g_stop_reason, pc, (unsigned long long)g_icount, g_unmapped);
            fflush(g_log);
        }
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Moonstone stopped unexpectedly.\n\nReason: %s\npc=%06x  frame-ic=%llu\n\n"
                 "A log was written next to the game:\n%s/moonstone.log\n\n"
                 "Please send that file so this can be fixed.",
                 g_stop_reason, pc, (unsigned long long)g_icount, g_exedir[0] ? g_exedir : ".");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Moonstone", msg, win);
    }
    if (pad) SDL_GameControllerClose(pad);
    if (g_audio_dev) { SDL_CloseAudioDevice(g_audio_dev); g_audio_dev = 0; }
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

/* ============================ AmigaOS HLE ============================ */
/* The game modules (nb/program/mog) use exec.library (memory) and dos.library
 * (file IO) in addition to banging the chips. We provide a minimal HLE: library
 * bases live in low memory with RTS-filled jump tables; the instruction hook
 * catches PC landing on a stub and dispatches the LVO to a C handler. File IO is
 * served from the extracted dataset (no floppy emulation). */
#define EXEC_BASE 0x00010000u
#define DOS_BASE  0x0000e000u
#define GFX_BASE  0x0000c000u
#define OTHER_BASE 0x0000a000u
#define HLE_LO    0x00009000u
#define HLE_HI    0x00010000u

/* Dataset folder served by the HLE file loader (by basename).  Default is the
 * dev location; main() repoints it at the bundled exe-relative `data/` folder
 * when that exists, and --dataset overrides it. */
static char g_dataset_buf[1200] = "../portable/moonstone_hdd";
static const char *g_dataset = g_dataset_buf;
static uint32_t g_heap = 0x00100000u;        /* bump allocator */
static uint32_t g_memtop = 0x001f0000u;

typedef struct { FILE *fp; long size; } HleFile;
static HleFile g_files[64];
static int g_oslog = 0;
static int g_ftime = 0;              /* --ftime: measure host wall-time per emulated frame */

static void read_cstr(uint32_t a, char *out, int max) {
    int i=0; for (; i<max-1; i++){ uint8_t c=r8(a+i); if(!c)break; out[i]=(char)c; } out[i]=0;
}
static uint32_t hle_reg(int r){ return m68k_get_reg(NULL,r); }
static void hle_setd0(uint32_t v){ m68k_set_reg(M68K_REG_D0, v); }

static uint32_t exec_lvo(int lvo) {
    switch (lvo) {
        case 552: case 408: {                 /* OpenLibrary / OldOpenLibrary */
            char nm[64]; read_cstr(hle_reg(M68K_REG_A1), nm, sizeof(nm));
            uint32_t base = OTHER_BASE;
            if (strstr(nm,"dos")) base=DOS_BASE; else if (strstr(nm,"graphics")) base=GFX_BASE;
            if (g_oslog && g_log) fprintf(g_log,"  exec OpenLibrary(\"%s\") -> %06x\n",nm,base);
            hle_setd0(base); return base;
        }
        case 414: hle_setd0(0); return 0;     /* CloseLibrary */
        case 198: case 684: {                 /* AllocMem / AllocVec */
            uint32_t size=hle_reg(M68K_REG_D0), req=hle_reg(M68K_REG_D1);
            uint32_t p=(g_heap+7)&~7u; g_heap=p+((size+7)&~7u);
            if (req & 0x10000) for(uint32_t i=0;i<size;i++) w8(p+i,0); /* MEMF_CLEAR */
            if (g_oslog && g_log) fprintf(g_log,"  exec AllocMem(%u) -> %06x\n",size,p);
            hle_setd0(p); return p;
        }
        case 210: case 690: hle_setd0(0); return 0;  /* FreeMem / FreeVec */
        case 216: hle_setd0(g_memtop - g_heap); return g_memtop-g_heap; /* AvailMem */
        case 294: hle_setd0(0x0c00); return 0x0c00;   /* FindTask -> this_task */
        case 132: case 138: case 120: case 126: return 0; /* Forbid/Permit/Disable/Enable */
        default:
            if (g_oslog && g_log) fprintf(g_log,"  exec LVO -%d (unimpl) pc-call\n", lvo);
            hle_setd0(0); return 0;
    }
    return 0;
}

static long find_file(const char *name, char *path, int max) {
    /* strip device/path prefix, keep basename */
    const char *b = name; for (const char *p=name; *p; p++) if (*p==':'||*p=='/'||*p=='\\') b=p+1;
    snprintf(path, max, "%s/%s", g_dataset, b);
    FILE *f=fopen(path,"rb"); if(!f) return -1;
    fseek(f,0,SEEK_END); long sz=ftell(f); fclose(f); return sz;
}

static uint32_t dos_lvo(int lvo) {
    switch (lvo) {
        case 30: {                            /* Open: D1=name D2=mode -> D0=handle */
            char nm[128]; read_cstr(hle_reg(M68K_REG_D1), nm, sizeof(nm));
            char path[256]; long sz=find_file(nm,path,sizeof(path));
            int h=0; for(int i=1;i<64;i++) if(!g_files[i].fp){h=i;break;}
            if (sz<0 || !h) { if(g_oslog&&g_log)fprintf(g_log,"  dos Open(\"%s\") NOT FOUND\n",nm); hle_setd0(0); return 0; }
            g_files[h].fp=fopen(path,"rb"); g_files[h].size=sz;
            if (g_oslog&&g_log) fprintf(g_log,"  dos Open(\"%s\") sz=%ld -> h%d\n",nm,sz,h);
            hle_setd0(h); return h;
        }
        case 36: { int h=hle_reg(M68K_REG_D1); if(h>0&&h<64&&g_files[h].fp){fclose(g_files[h].fp);g_files[h].fp=0;} hle_setd0(1); return 1; } /* Close */
        case 42: {                            /* Read: D1=h D2=buf D3=len -> D0=actual */
            int h=hle_reg(M68K_REG_D1); uint32_t buf=hle_reg(M68K_REG_D2), len=hle_reg(M68K_REG_D3);
            if (h<=0||h>=64||!g_files[h].fp){hle_setd0(-1);return -1;}
            uint8_t *tmp=malloc(len); size_t got=fread(tmp,1,len,g_files[h].fp);
            for(size_t i=0;i<got;i++) w8(buf+i,tmp[i]); free(tmp);
            if (g_oslog&&g_log) fprintf(g_log,"  dos Read(h%d,%06x,%u) -> %zu\n",h,buf,len,got);
            hle_setd0((uint32_t)got); return (uint32_t)got;
        }
        case 66: {                            /* Seek: D1=h D2=pos D3=mode -> D0=oldpos */
            int h=hle_reg(M68K_REG_D1); int32_t pos=(int32_t)hle_reg(M68K_REG_D2), mode=(int32_t)hle_reg(M68K_REG_D3);
            if(h<=0||h>=64||!g_files[h].fp){hle_setd0(-1);return -1;}
            long old=ftell(g_files[h].fp);
            int wh = mode<0?SEEK_SET : mode==0?SEEK_CUR : SEEK_END;
            fseek(g_files[h].fp,pos,wh); hle_setd0((uint32_t)old); return old;
        }
        case 54: hle_setd0(0); return 0;      /* Input */
        case 60: hle_setd0(0); return 0;      /* Output */
        default:
            if (g_oslog&&g_log) fprintf(g_log,"  dos LVO -%d (unimpl)\n", lvo);
            hle_setd0(0); return 0;
    }
}

static void hle_dispatch(uint32_t pc) {
    if (pc >= 0xF000) exec_lvo((int)(EXEC_BASE - pc));
    else if (pc >= 0xD000) dos_lvo((int)(DOS_BASE - pc));
    else if (pc >= 0xB000) { if(g_oslog&&g_log)fprintf(g_log,"  gfx LVO -%d\n",(int)(GFX_BASE-pc)); hle_setd0(0); }
    else { if(g_oslog&&g_log)fprintf(g_log,"  other LVO -%d\n",(int)(OTHER_BASE-pc)); hle_setd0(0); }
}

/* ===================== nb floppy-loader HLE (bypass OFS/trackdisk) =========
 * nb's hunk14 orchestrator (@0x2ea90) is itself a full AmigaDOS LoadSeg: it
 * pulls the target file as a raw byte stream (HUNK_HEADER..relocs) and relocates
 * it.  The bytes come from THREE routines we replace so no disk/OFS is emulated:
 *   0x2ca94  LOAD-BY-NAME: a0 = filename C-string. Opens the file and resets a
 *            sequential read cursor.  (Original: OFS dir-hash lookup + block read.)
 *   0x2cda0  STREAM-READ:  a0 = dest, d0 = BYTE count. Copies the next d0 bytes
 *            of the open file into [a0] and advances the cursor.  (Original: walk
 *            OFS 488-byte data blocks via 0x2bdba.)
 *   0x2cf06  STREAM-SKIP:  d0 = BYTE count. Advances the cursor without copying
 *            (the orchestrator uses it to skip each hunk's size longword).
 * Each ends in an RTS we synthesize in C, so the original disk bodies never run.
 * This is fully general: program, Mog and every asset are loaded by the same
 * trio.  `program` reuses these hunks verbatim but relocated (delta -0x1cd8), so
 * its own loader copies (0x2adbc/0x2b0c8/0x2b22e) are trapped to the same C. */
static uint8_t *g_loadbuf = NULL;     /* current open file's bytes */
static long     g_loadsize = 0;       /* total size */
static long     g_loadpos  = 0;       /* read cursor */
static char     g_loadname[64] = "";

/* The g_loadbuf streamer is serialized by NAME+cursor and re-read on load (it is
 * just an in-memory copy of a dataset file), so it's fully save-safe and stays
 * "open" for the whole session -- NOT a reason to block saving.  The dos.library
 * Open/Read/Close API (g_files[]) DOES hold raw host FILE* with no stored name,
 * so a quicksave is refused only while one of those handles is open (rare; the
 * game streams area data via g_loadbuf / trackdisk, not these). */
static int load_in_progress(void) {
    for (int i = 1; i < 64; i++) if (g_files[i].fp) return 1;
    return 0;
}

/* Pop the supervisor/active-stack return address and resume there (mimic RTS). */
static void hle_rts(void) {
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    uint32_t ret = r32(sp);
    m68k_set_reg(M68K_REG_SP, sp + 4);
    m68k_set_reg(M68K_REG_PC, ret);
}

/* 0x2ca94: load file named by a0 into g_loadbuf; reset cursor. */
static void hle_load_by_name(void) {
    char nm[64]; read_cstr(hle_reg(M68K_REG_A0), nm, sizeof(nm));
    char path[256]; long sz = find_file(nm, path, sizeof(path));
    if (g_loadbuf) { free(g_loadbuf); g_loadbuf = NULL; }
    g_loadsize = 0; g_loadpos = 0; g_loadname[0] = 0;
    if (sz < 0) {
        if (g_log) fprintf(g_log, "HLE load-by-name \"%s\" -> NOT FOUND\n", nm);
        /* result flag <0 = failure. nb's copy @0x2ca22, program's @0x2ad4a. */
        w16(0x2ca22, 0xffff); w16(0x2ad4a, 0xffff);
        hle_rts();
        return;
    }
    FILE *f = fopen(path, "rb");
    g_loadbuf = (uint8_t*)malloc(sz);
    g_loadsize = (long)fread(g_loadbuf, 1, sz, f);
    fclose(f);
    snprintf(g_loadname, sizeof(g_loadname), "%s", nm);
    if (!strcmp(nm, "program")) g_program_served = 1;
    /* success state the original leaves: stream cursors zeroed, status>=0.
     * Write both nb's (0x2ca22/24) and program's (0x2ad4a/4c) result globals. */
    w16(0x2ca22, 0x0000); w16(0x2ca24, 0x0000);
    w16(0x2ad4a, 0x0000); w16(0x2ad4c, 0x0000);
    if (g_log) fprintf(g_log, "HLE load-by-name \"%s\" sz=%ld -> served from dataset\n", nm, g_loadsize);
    hle_rts();
}

/* 0x2cda0: copy next d0 bytes of the open file into a0; advance cursor. */
static void hle_stream_read(void) {
    uint32_t dst = hle_reg(M68K_REG_A0);
    uint32_t n   = hle_reg(M68K_REG_D0);
    long avail = g_loadbuf ? (g_loadsize - g_loadpos) : 0;
    uint32_t cnt = (long)n <= avail ? n : (avail > 0 ? (uint32_t)avail : 0);
    for (uint32_t i = 0; i < cnt; i++) w8(dst + i, g_loadbuf[g_loadpos + i]);
    g_loadpos += cnt;
    if (g_log && g_streamlog < 40) {
        fprintf(g_log, "HLE stream-read \"%s\" dst=%06x n=%u -> %u (pos=%ld/%ld)\n",
                g_loadname, dst, n, cnt, g_loadpos, g_loadsize);
        g_streamlog++;
    }
    hle_rts();
}

/* 0x2cf06: advance the stream cursor by d0 bytes WITHOUT copying (skip the
 * per-hunk size longword the orchestrator doesn't keep). Leaves d0 intact. */
static void hle_stream_skip(void) {
    uint32_t n = hle_reg(M68K_REG_D0);
    long avail = g_loadbuf ? (g_loadsize - g_loadpos) : 0;
    uint32_t cnt = (long)n <= avail ? n : (avail > 0 ? (uint32_t)avail : 0);
    g_loadpos += cnt;
    if (g_log && g_streamlog < 40) {
        fprintf(g_log, "HLE stream-skip \"%s\" n=%u (pos=%ld/%ld)\n",
                g_loadname, n, g_loadpos, g_loadsize);
        g_streamlog++;
    }
    hle_rts();
}

static void setup_amigaos(uint32_t sp) {
    memset(g_ram, 0, 0x20000);                /* vectors + low mem + lib region */
    /* fill HLE stub region with RTS so any LVO slot returns */
    for (uint32_t a=HLE_LO; a<HLE_HI; a+=2) w16(a, 0x4e75);
    w32(0x0004, EXEC_BASE);                   /* SysBase */
    /* minimal ExecBase fields */
    w32(EXEC_BASE+0x114, 0x0c00);             /* ThisTask */
    w32(EXEC_BASE+0x22a, 0);                  /* LibList etc left null */
    w16(EXEC_BASE+0x126, 0x0000);             /* AttnFlags: 68000 */
    /* a Task at 0x0c00 with stack bounds */
    w32(0x0c00+0x3a, 0x00070000); w32(0x0c00+0x3e, 0x00080000); w32(0x0c00+0x36, sp);
    /* exception-vector guard */
    for (uint32_t v=0x08; v<0x100; v+=4) if (r32(v)==0) w32(v, 0x00000500);
    w16(0x500, 0x4afc);
    g_heap = 0x00100000u;
    memset(g_files, 0, sizeof(g_files));
}

/* Should a VERTB (level-3) interrupt be delivered this frame?
 *
 * This is the heart of the interrupt model.  The boot chain has two distinct
 * phases with opposite needs:
 *   - `nb` (the loader) and the cracktro POLL the raster (cmpi.b/VPOSR loops)
 *     and never install a real VBlank handler.  Firing VERTB at them lands on
 *     nb's stub L3 handler (0x2b82e) or the bare exception guard (0x500) and
 *     corrupts the active stack -> the rts-pops-0 crash at ic~739k.
 *   - `program` (the front-end) and `Mog` are INTERRUPT-DRIVEN: they install a
 *     real L3 autovector at [0x6c] (program's is 0x29ff2) and enable INTEN
 *     master + VERTB.  Their main loop stalls forever without a VBlank IRQ.
 *
 * So we GATE on "has a real game-owned VBlank handler been installed and armed".
 * This is the DEFAULT (correct out of the box).  `--novblank` forces the IRQ
 * off entirely (pure-polling debug mode); `--forcevblank` fires every frame
 * regardless (old broken behaviour, kept for A/B testing).
 *
 * The gate is structural, not address-hard-coded: any handler that lives inside
 * the loaded game image (>= image base, below the work pools) and is NOT one of
 * the known loader/exec stubs qualifies, so it also works for Mog. */
static int vertb_gate(void) {
    if (g_pollonly)    return 0;               /* --pollonly: never inject     */
    if (g_forcevblank) return 1;               /* --forcevblank: always inject */
    (void)g_novblank;                          /* --novblank: deprecated no-op (gate is default now) */
    if (!g_os) return 1;                        /* cracktro: validated with unconditional VERTB */
    /* `--os` boot chain (nb -> program -> Mog): only deliver once a real
     * VBlank handler is installed AND armed, else nb's poll-loop stack is
     * corrupted by IRQ frames hitting its stub handler / the boot guard. */
    uint16_t ena = g_custom[0x09a>>1];
    if ((ena & 0x4000) == 0) return 0;         /* master INTEN off             */
    if ((ena & 0x0020) == 0) return 0;         /* VERTB source not enabled     */
    uint32_t l3 = r32(0x6c);                   /* installed L3 autovector      */
    if (l3 < 0x21000 || l3 >= 0x60000) return 0;   /* not a game-owned handler */
    if (l3 == 0x2b82e) return 0;               /* nb's loader stub: not ready  */
    if (l3 == 0x000500) return 0;              /* bare exception guard         */
    return 1;
}

/* ---- scripted input timeline (--script) ------------------------------------
 * Drives the game's input from a frame-keyed script so we can walk it from the
 * intro to gameplay without a live window.  Syntax: comma-separated events
 *   FRAME:KEYS   (KEYS held from FRAME until the next event's frame)
 * KEYS is a string of: u d l r (direction), f (fire: BOTH CIA-A PRA bit6 /FIR0
 * and bit7 /FIR1, covering mouse-button and joystick-fire), '.'/empty=neutral.
 * Directions drive the digital joystick (JOY1DAT) AND nudge the mouse counter
 * (JOY0DAT) by +/-MOUSE_STEP each read, so the script works whether the active
 * screen polls the joystick or the mouse.  Example:
 *   --script "300:f,360:.,2000:r,2200:f" */
#define MAX_SCRIPT 64
typedef struct { int frame; int u,d,l,r,fire,rest; } ScriptEv;
static ScriptEv g_script[MAX_SCRIPT];
static int      g_nscript = 0;
#define MOUSE_STEP 3

static void parse_script(const char *s) {
    while (*s && g_nscript < MAX_SCRIPT) {
        char *end;
        long fr = strtol(s, &end, 10);
        if (end == s) break;
        if (*end != ':') break;
        s = end + 1;
        ScriptEv ev = { (int)fr, 0,0,0,0,0,0 };
        while (*s && *s != ',') {
            switch (*s) {
                case 'u': ev.u=1; break; case 'd': ev.d=1; break;
                case 'l': ev.l=1; break; case 'r': ev.r=1; break;
                case 'f': ev.fire=1; break; case 'e': ev.rest=1; break; case '.': break;
                default: break;
            }
            s++;
        }
        g_script[g_nscript++] = ev;
        if (*s == ',') s++;
    }
}

/* Apply the script at frame `fr`: find the latest event whose frame <= fr and
 * set the input globals accordingly. */
static void apply_script(int fr) {
    int idx = -1;
    for (int i = 0; i < g_nscript; i++) if (g_script[i].frame <= fr) idx = i; else break;
    if (idx < 0) { g_ji_up=g_ji_dn=g_ji_lf=g_ji_rt=0; g_fire=g_fire2=0; g_mouse_dx=g_mouse_dy=0; return; }
    ScriptEv *e = &g_script[idx];
    g_ji_up=e->u; g_ji_dn=e->d; g_ji_lf=e->l; g_ji_rt=e->r;
    g_fire = e->fire; g_fire2 = e->fire;
    g_mouse_dx = (e->r?MOUSE_STEP:0) - (e->l?MOUSE_STEP:0);
    g_mouse_dy = (e->d?MOUSE_STEP:0) - (e->u?MOUSE_STEP:0);
    /* 'e' token = a ONE-SHOT REST/skip-turn pulse (like a single 'E' keypress): fire g_rest_request
     * only on the frame this event first becomes active, not every frame it stays latest. */
    { static int last_idx = -1; if (idx != last_idx) { if (e->rest) g_rest_request = 1; last_idx = idx; } }
}

/* ---- scripted typed text (--type "FRAME:TEXT,...") --------------------------
 * Headless analogue of host keydown: at FRAME, enqueue each character of TEXT
 * (one per frame is unnecessary -- the name-entry FSM drains one per poll, so a
 * burst enqueued in the same frame is typed in order over the next polls).  Chars:
 * letters/digits/space map via keysym_to_idx; '<' = Backspace (index 0x0e); '!'
 * = Return/confirm (index 0x1c).  Used only to verify the name field headlessly;
 * the live SDL path enqueues from real keydown events. */
typedef struct { int frame; char text[24]; int fired; } TypeEv;
static TypeEv g_typeev[16];
static int    g_ntypeev = 0;
static void parse_type(const char *s) {
    while (*s && g_ntypeev < 16) {
        char *end; long fr = strtol(s, &end, 10);
        if (end==s || *end!=':') break;
        s = end + 1;
        TypeEv *e = &g_typeev[g_ntypeev];
        e->frame = (int)fr; e->fired = 0; int n = 0;
        while (*s && *s != ',' && n < (int)sizeof(e->text)-1) e->text[n++] = *s++;
        e->text[n] = 0; g_ntypeev++;
        if (*s==',') s++;
    }
}
static void apply_type(int fr) {
    for (int i = 0; i < g_ntypeev; i++) {
        TypeEv *e = &g_typeev[i];
        if (e->fired || e->frame > fr) continue;
        for (const char *p = e->text; *p; p++) {
            uint8_t ix;
            if      (*p == '<') ix = 0x0e;                       /* backspace */
            else if (*p == '!') ix = 0x1c;                       /* confirm   */
            else {
                int c = *p; if (c >= 'A' && c <= 'Z') c += 32;   /* fold to lowercase keysym */
                ix = keysym_to_idx(c);
            }
            if (ix) keyq_push(ix);
        }
        e->fired = 1;
    }
}

/* Disk-swap timeline (--diskat "FRAME:N,..."): insert ADF index N (0=Disk1,
 * 1=Disk2, 2=Disk3) into drive 0 at FRAME, asserting /CHNG low so the game's
 * disk-change check fires; the next head step clears it.  The game prompts
 * "Please insert Disk B ... Press fire when done" and waits for fire, so a swap
 * event should land a few frames before the matching fire press in --script. */
typedef struct { int frame, disk; } DiskEv;
static DiskEv g_diskev[16];
static int    g_ndiskev = 0;
static void parse_diskat(const char *s) {
    g_autoswap = 0;   /* manual disk control: do not auto-handle swap prompts */
    while (*s && g_ndiskev < 16) {
        char *end; long fr = strtol(s, &end, 10);
        if (end==s || *end!=':') break;
        s = end + 1;
        long dk = strtol(s, &end, 10);
        s = end;
        g_diskev[g_ndiskev].frame=(int)fr; g_diskev[g_ndiskev].disk=(int)dk; g_ndiskev++;
        if (*s==',') s++;
    }
}
/* --poke "ADDR:VALUE[:b|w|l][@PC],..." (DEBUG-ONLY, g_os-gated): set up end-game
 * state for verification.  ADDR/VALUE/PC are C-style numbers (0x.. hex ok).
 * Size defaults to word (w); @PC selects when the write is applied (default
 * 0x21206 = program's main dispatch loop, which spins continuously once the
 * player record is live).  Off by default; inert unless this flag is passed. */
static void parse_poke(const char *s) {
    while (*s && g_npokes < MAX_POKES) {
        char *end;
        unsigned long addr = strtoul(s, &end, 0);
        if (end==s || *end!=':') break;
        s = end + 1;
        unsigned long val = strtoul(s, &end, 0);
        s = end;
        int size = 2;                     /* default word */
        uint32_t at_pc = 0x21206u;        /* default: program main dispatch loop */
        if (*s==':') {                    /* optional size suffix b/w/l */
            s++;
            if      (*s=='b'||*s=='B') size=1;
            else if (*s=='l'||*s=='L') size=4;
            else                       size=2;
            s++;
        }
        if (*s=='@') {                    /* optional gate-PC */
            s++;
            at_pc = (uint32_t)strtoul(s, &end, 0);
            s = end;
        }
        g_pokes[g_npokes].addr=(uint32_t)addr; g_pokes[g_npokes].val=(uint32_t)val;
        g_pokes[g_npokes].size=size; g_pokes[g_npokes].at_pc=at_pc;
        g_pokes[g_npokes].applied=0; g_npokes++;
        if (*s==',') s++;
    }
}
/* Per-frame driver for the seamless auto-swap: count down the "settle" window so
 * the newly-inserted disk gets re-read + validated before we synthesise a fire.
 * The actual fire press/release is driven by PC in moon_instr_hook (when the game
 * is genuinely sitting in the "Press fire" wait at 0x22fd0) so the timing can't
 * race ahead of the wait. */
static void autoswap_tick(void) {
    if (!g_autoswap || !g_autoswap_armed) return;
    if (g_autoswap_settle > 0) g_autoswap_settle--;
}

static int g_diskev_done[16];
static void apply_diskat(int fr) {
    for (int i=0;i<g_ndiskev;i++) {
        if (!g_diskev_done[i] && fr >= g_diskev[i].frame) {
            g_diskev_done[i]=1;
            int n=g_diskev[i].disk;
            if (n>=0 && n<3 && g_adf[n]) {
                g_disk_inserted = n;
                g_chng_low = 1;             /* signal disk changed */
                if (g_log) fprintf(g_log, "DISK-SWAP @frame %d -> Disk%d\n", fr, n+1);
            }
        }
    }
}

/* ===================== save-state (quicksave / quickload) =====================
 * This port is a whole-machine emulator, so "save progress" = freeze the entire
 * machine to a file and thaw it back: the 68000 context, all 2MB of RAM, and
 * every piece of mutable chip / CIA / Paula / HLE state.  No knowledge of the
 * game's own data structures is needed (the 1991 game had no save format), and
 * it works anywhere -- even mid-combat.  Single quicksave slot at
 * <exedir>/moonstone.sav.  F5 saves, F9 loads.  The original Amiga release had
 * NO save/password at all (one of its most-criticized flaws); this fixes that. */
#define SAVE_MAGIC   "MOONSAVE"
#define SAVE_VERSION 2u    /* v2: portable CPU REGISTERS instead of Musashi's context blob.
                            * v1 embedded the context, which contains HOST function pointers,
                            * so a v1 save loaded by a different build dereferenced stale
                            * pointers -> crash.  v2 saves only register VALUES, so a save
                            * survives rebuilds.  v1 saves are rejected by the version check. */

/* CPU architectural state as a flat list of register VALUES (no host pointers).
 * This array's order IS the on-disk layout. */
static const int SAVE_REGS[] = {
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3,
    M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3,
    M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
    M68K_REG_PC, M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP, M68K_REG_VBR,
};
#define SAVE_NREGS ((int)(sizeof(SAVE_REGS)/sizeof(SAVE_REGS[0])))

/* ONE (de)serializer drives both directions so save & load can never drift out
 * of sync.  dir=1 => write, dir=0 => read.  Returns 1 on full success. */
static int sv_blob(FILE *f, int dir, void *p, size_t n) {
    return dir ? (fwrite(p,1,n,f)==n) : (fread(p,1,n,f)==n);
}
static int sv_serialize(FILE *f, int dir, uint32_t *regs, int nregs) {
    int ok = 1;
    ok &= sv_blob(f, dir, regs, (size_t)nregs*sizeof(uint32_t)); /* 68000 CPU registers (portable) */
    ok &= sv_blob(f, dir, g_ram, RAM_SIZE);            /* all 2MB chip RAM            */
    ok &= sv_blob(f, dir, g_custom, sizeof(g_custom)); /* DFF000 custom registers     */
    ok &= sv_blob(f, dir, g_ciaa, sizeof(g_ciaa));
    ok &= sv_blob(f, dir, g_ciab, sizeof(g_ciab));
    ok &= sv_blob(f, dir, &g_ca, sizeof(g_ca));        /* CIA-A live timers + latches */
    ok &= sv_blob(f, dir, &g_cb, sizeof(g_cb));        /* CIA-B live timers + latches */
    ok &= sv_blob(f, dir, g_paula, sizeof(g_paula));   /* 4 audio channels            */
    ok &= sv_blob(f, dir, &g_cycles, sizeof(g_cycles));
    ok &= sv_blob(f, dir, &g_icount, sizeof(g_icount));
    ok &= sv_blob(f, dir, &g_frame_cycle, sizeof(g_frame_cycle));
    ok &= sv_blob(f, dir, &g_vpos, sizeof(g_vpos));
    ok &= sv_blob(f, dir, &g_hpos, sizeof(g_hpos));
    ok &= sv_blob(f, dir, &g_beam_line, sizeof(g_beam_line));
    ok &= sv_blob(f, dir, &g_beam_hpos, sizeof(g_beam_hpos));
    ok &= sv_blob(f, dir, &g_heap, sizeof(g_heap));    /* HLE bump-allocator pointer  */
    ok &= sv_blob(f, dir, &g_disk_inserted, sizeof(g_disk_inserted));
    ok &= sv_blob(f, dir, &g_chng_low, sizeof(g_chng_low));
    ok &= sv_blob(f, dir, &g_dsk_cyl, sizeof(g_dsk_cyl));     /* floppy head position (matters mid-load) */
    ok &= sv_blob(f, dir, &g_dsk_head, sizeof(g_dsk_head));
    ok &= sv_blob(f, dir, &g_dsk_sel, sizeof(g_dsk_sel));
    ok &= sv_blob(f, dir, &g_dsk_known, sizeof(g_dsk_known));
    ok &= sv_blob(f, dir, &g_dsk_reads, sizeof(g_dsk_reads));
    ok &= sv_blob(f, dir, &g_autoswap_armed, sizeof(g_autoswap_armed));
    ok &= sv_blob(f, dir, &g_autoswap_settle, sizeof(g_autoswap_settle));
    ok &= sv_blob(f, dir, &g_blt_busy_until, sizeof(g_blt_busy_until));
    ok &= sv_blob(f, dir, &g_blt_busy_scope, sizeof(g_blt_busy_scope));
    ok &= sv_blob(f, dir, &g_blit_count, sizeof(g_blit_count));
    ok &= sv_blob(f, dir, g_diskev_done, sizeof(g_diskev_done));
    /* dos-HLE streamer: save the file NAME + cursor; the buffer itself (a host
     * pointer) is reconstructed on load by re-reading the dataset file. */
    ok &= sv_blob(f, dir, g_loadname, sizeof(g_loadname));
    ok &= sv_blob(f, dir, &g_loadpos, sizeof(g_loadpos));
    ok &= sv_blob(f, dir, &g_loadsize, sizeof(g_loadsize));
    ok &= sv_blob(f, dir, &g_program_served, sizeof(g_program_served));
    return ok;
}

static int save_state(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t ver = SAVE_VERSION, ram = RAM_SIZE, nregs = SAVE_NREGS;
    uint32_t regs[SAVE_NREGS];
    for (int i = 0; i < SAVE_NREGS; i++) regs[i] = m68k_get_reg(NULL, SAVE_REGS[i]);
    int ok = (fwrite(SAVE_MAGIC,1,8,f)==8);
    ok = ok && (fwrite(&ver,4,1,f)==1) && (fwrite(&ram,4,1,f)==1) && (fwrite(&nregs,4,1,f)==1);
    ok = ok && sv_serialize(f, 1, regs, SAVE_NREGS);
    fclose(f);
    if (g_log) fprintf(g_log, "SAVESTATE %s ok=%d ic=%llu\n", path, ok, (unsigned long long)g_icount);
    return ok;
}

static int load_state(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[8]; uint32_t ver=0, ram=0, nregs=0;
    int ok = (fread(magic,1,8,f)==8) && memcmp(magic,SAVE_MAGIC,8)==0;
    ok = ok && (fread(&ver,4,1,f)==1) && ver==SAVE_VERSION;        /* v1 (context-blob) saves rejected here */
    ok = ok && (fread(&ram,4,1,f)==1) && ram==RAM_SIZE;
    ok = ok && (fread(&nregs,4,1,f)==1) && nregs==(uint32_t)SAVE_NREGS;
    if (!ok) { fclose(f); return 0; }
    uint32_t regs[SAVE_NREGS];
    ok = sv_serialize(f, 0, regs, SAVE_NREGS);
    fclose(f);
    if (ok) {
        /* Restore the CPU: SR FIRST (sets supervisor/user mode + IRQ mask so A7 maps
         * to the correct stack), then all other registers.  Pure values, no pointers. */
        for (int i = 0; i < SAVE_NREGS; i++) if (SAVE_REGS[i]==M68K_REG_SR) m68k_set_reg(M68K_REG_SR, regs[i]);
        for (int i = 0; i < SAVE_NREGS; i++) if (SAVE_REGS[i]!=M68K_REG_SR) m68k_set_reg(SAVE_REGS[i], regs[i]);
        paula_sidecar_reset();   /* Paula reload-protocol statics live outside the blob: rebuild them
                                  * from the just-restored registers (see paula_sidecar_reset) */
    }
    /* Rebuild the streamer's in-memory file copy: g_loadbuf is a host pointer that
     * wasn't serialized -- re-read the (deterministic) dataset file by name and
     * restore the cursor that sv_serialize just loaded into g_loadpos/g_loadsize. */
    if (ok) {
        if (g_loadbuf) { free(g_loadbuf); g_loadbuf = NULL; }
        if (g_loadname[0]) {
            char p[256]; long sz = find_file(g_loadname, p, sizeof(p));
            FILE *lf = (sz >= 0) ? fopen(p, "rb") : NULL;
            if (lf) {
                g_loadbuf = (uint8_t*)malloc(sz > 0 ? sz : 1);
                long got = g_loadbuf ? (long)fread(g_loadbuf, 1, sz, lf) : 0;
                fclose(lf);
                if (got != g_loadsize && g_log)
                    fprintf(g_log, "LOADSTATE warn: streamer '%s' re-read %ld != saved %ld\n",
                            g_loadname, got, g_loadsize);
            } else if (g_log) {
                fprintf(g_log, "LOADSTATE warn: streamer file '%s' not found in dataset\n", g_loadname);
            }
        }
    }
    if (g_log) fprintf(g_log, "LOADSTATE %s ok=%d ic=%llu\n", path, ok, (unsigned long long)g_icount);
    return ok;
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv) {
    const char *mod = NULL;          /* default chosen below (exe-relative) */
    uint32_t base = 0x21000;
    uint64_t steps = 5000000;
    int trace_n = 0;
    uint32_t sr = 0x2000; /* supervisor, mask 0 (got the loader chain furthest) */
    const char *logpath = NULL;   /* default: <exedir>/moonstone.log (set after exedir) */
    const char *dumppath = NULL;
    const char *ramdump = NULL;
    int frames = 600;
    int press_frame = -1;
    int clickthru = 0;
    int dumpevery = 0;
    int sdl = 0, scale = 3;
    const char *wavpath = NULL;
    int dataset_set = 0;             /* --dataset given explicitly? */
    int diskdir_set = 0;             /* --diskdir given explicitly? */
    const char *modarg = NULL;       /* --mod value if given */
    const char *savestate_path = NULL; int savestate_at = -1;  /* --savestate-at FRAME FILE (validation) */
    const char *loadstate_path = NULL; int loadstate_at = -1;   /* --loadstate FILE / --loadstate-at FRAME FILE (validation) */
    uint32_t disasm_addr = 0; int disasm_len = 0;               /* --disasm ADDR LEN (dev: dump 68k code at end of run) */

    compute_exedir(argv && argv[0] ? argv[0] : "");

    /* Double-click default: no args => play the game (SDL window + OS HLE),
     * which is what a distributable user wants.  Any explicit flag is honoured. */
    if (argc <= 1) { sdl = 1; g_os = 1; g_oslog = 1; }

    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--mod")&&i+1<argc) modarg=argv[++i];
        else if (!strcmp(argv[i],"--base")&&i+1<argc) base=(uint32_t)strtoul(argv[++i],0,0);
        else if (!strcmp(argv[i],"--steps")&&i+1<argc) steps=strtoull(argv[++i],0,0);
        else if (!strcmp(argv[i],"--trace")&&i+1<argc) trace_n=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--sr")&&i+1<argc) sr=(uint32_t)strtoul(argv[++i],0,0);
        else if (!strcmp(argv[i],"--log")&&i+1<argc) logpath=argv[++i];
        else if (!strcmp(argv[i],"--flow")) g_flow=1;
        else if (!strcmp(argv[i],"--trace-from")&&i+1<argc) g_trace_from=strtoull(argv[++i],0,0);
        else if (!strcmp(argv[i],"--tracen")&&i+1<argc) g_trace_budget=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--traceregs")) g_traceregs=1;
        else if (!strcmp(argv[i],"--frames")&&i+1<argc) frames=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--dump")&&i+1<argc) dumppath=argv[++i];
        else if (!strcmp(argv[i],"--dumpram")&&i+1<argc) ramdump=argv[++i];
        else if (!strcmp(argv[i],"--dumpat")&&i+2<argc) { g_dumpat=strtoull(argv[++i],0,0); g_dumpat_path=argv[++i]; }
        else if (!strcmp(argv[i],"--press")&&i+1<argc) press_frame=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--clickthru")) clickthru=1;
        else if (!strcmp(argv[i],"--dumpevery")&&i+1<argc) dumpevery=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--sdl")) sdl=1;
        else if (!strcmp(argv[i],"--version")||!strcmp(argv[i],"-v")) { printf("%s\n", MOON_ATTRIB); return 0; }
        else if (!strcmp(argv[i],"--scale")&&i+1<argc) scale=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--memlog")) g_memlog=1;
        else if (!strcmp(argv[i],"--hardpan")) g_xfeed=0;
        else if (!strcmp(argv[i],"--audioblend")&&i+1<argc) { g_xfeed=atoi(argv[++i]); if(g_xfeed<0)g_xfeed=0; if(g_xfeed>50)g_xfeed=50; }
        else if (!strcmp(argv[i],"--os")) { g_os=1; g_oslog=1; }
        else if (!strcmp(argv[i],"--dataset")&&i+1<argc) { snprintf(g_dataset_buf,sizeof(g_dataset_buf),"%s",argv[++i]); dataset_set=1; }
        else if (!strcmp(argv[i],"--diskdir")&&i+1<argc) { g_diskdir=argv[++i]; diskdir_set=1; }
        else if (!strcmp(argv[i],"--noautoswap")) g_autoswap=0;   /* disable seamless disk-swap */
        else if (!strcmp(argv[i],"--noflopdelay")) g_dsk_fastwait=0; /* keep the real ~8s floppy motor/seek waits */
        else if (!strcmp(argv[i],"--skipat")&&i+1<argc) g_skipat=atoi(argv[++i]); /* diag: auto intro-skip at frame N */
        else if (!strcmp(argv[i],"--stallat")&&i+1<argc) g_stallat=atoi(argv[++i]); /* diag: simulate a host stall at frame N */
        else if (!strcmp(argv[i],"--flopdelay")) g_dsk_fastwait=0;   /* alias */
        else if (!strcmp(argv[i],"--watch")&&i+1<argc) g_watch=(uint32_t)strtoul(argv[++i],0,0);
        else if (!strcmp(argv[i],"--watchrd")&&i+1<argc) g_watchrd=(uint32_t)strtoul(argv[++i],0,0);
        else if (!strcmp(argv[i],"--l3trace")&&i+1<argc) g_l3trace=(uint32_t)strtoul(argv[++i],0,0);
        else if (!strcmp(argv[i],"--rdbg")) g_rdbg=1;
        else if (!strcmp(argv[i],"--rawcapture")) g_rawcapture=1;  /* diag: bypass both render heuristics */
        else if (!strcmp(argv[i],"--maplog")) g_maplog=1;
        else if (!strcmp(argv[i],"--delvlog")) g_delvlog=1;   /* TEMP diag: trace moonstone-delivery altar handler */
        else if (!strcmp(argv[i],"--livedump")) g_livedump=1; /* TEMP diag: force the F10 displayed-frame recorder on */
        else if (!strcmp(argv[i],"--spritex")&&i+1<argc) g_sprite_xoff=atoi(argv[++i]);  /* sprite-x origin calibration */
        else if (!strcmp(argv[i],"--recover")) g_recover=1;        /* re-enable the (now-unneeded) recovery heuristic */
        else if (!strcmp(argv[i],"--norecover")) { /* deprecated no-op: recovery is OFF by default now */ }
        else if (!strcmp(argv[i],"--novblank")) g_novblank=1;   /* deprecated: gate is now default */
        else if (!strcmp(argv[i],"--pollonly")) g_pollonly=1;
        else if (!strcmp(argv[i],"--forcevblank")) g_forcevblank=1;
        else if (!strcmp(argv[i],"--inlog")) g_inlog=1;
        else if (!strcmp(argv[i],"--bltlog")&&i+2<argc) { g_bltlog=1; g_bltlog_from=strtoull(argv[++i],0,0); g_bltlog_to=strtoull(argv[++i],0,0); }
        else if (!strcmp(argv[i],"--cflog")) g_cflog=1;
        else if (!strcmp(argv[i],"--script")&&i+1<argc) parse_script(argv[++i]);
        else if (!strcmp(argv[i],"--type")&&i+1<argc) parse_type(argv[++i]);
        else if (!strcmp(argv[i],"--diskat")&&i+1<argc) parse_diskat(argv[++i]);
        else if (!strcmp(argv[i],"--poke")&&i+1<argc) parse_poke(argv[++i]);   /* debug-only end-game state setup (g_os-gated) */
        else if (!strcmp(argv[i],"--wav")&&i+1<argc) wavpath=argv[++i];
        else if (!strcmp(argv[i],"--audlog")) g_audlog=1;
        /* Off-switch for the ISR-driven deferred-reload fix (see paula_tick_one).
         * The fix is ON by default; this only exists for A/B audio debugging. */
        else if (!strcmp(argv[i],"--noisrwait")) g_isr_wait=0;
        else if (!strcmp(argv[i],"--nosfxdblguard")) g_sfx_dblguard=0;  /* A/B: disable the SFX one-shot double-guard */
        else if (!strcmp(argv[i],"--notaskfix")) g_tasklist_fix=0;           /* A/B: disable the task-list root fix (dedup + effect-only removal + cursor value-removal hardening) */
        else if (!strcmp(argv[i],"--noaud1fix")) g_aud1_dmafix=0;            /* A/B: disable the AUD1 stray-DMACON-disable fix (0x4371c) */
        else if (!strcmp(argv[i],"--nochokefix")) g_choke_haul_fix=0;        /* A/B: disable the canopy-choke off-screen-haul fix (0x272a8) */
        else if (!strcmp(argv[i],"--trumpetmode")&&i+1<argc) g_trumpetmode=atoi(argv[++i]);  /* 0=off 1=mute-echo 2=unison (intro trumpet diag) */
        else if (!strcmp(argv[i],"--lpf")&&i+1<argc) { double fc=atof(argv[++i]); g_lpf_a = fc>0.0 ? (int)(65536.0/(44100.0/(6.2831853*fc)+1.0)+0.5) : 0; }  /* Amiga output RC filter cutoff Hz (0=off) */
        else if (!strcmp(argv[i],"--boomlp")&&i+1<argc) { double fc=atof(argv[++i]); g_boomlp_a = fc>0.0 ? (int)(65536.0/(44100.0/(6.2831853*fc)+1.0)+0.5) : 0; }  /* per-channel de-click LP on the AUD2 boom, Hz (0=off) */
        else if (!strcmp(argv[i],"--ledfilter")&&i+1<argc) { led_set_cutoff(atof(argv[++i])); }  /* A500 LED Butterworth cutoff Hz (0=off=brightest) */
        else if (!strcmp(argv[i],"--attackramp")&&i+1<argc) g_attack_ramp=atoi(argv[++i]);  /* note-onset de-click ramp length in host samples (0=off) */
        else if (!strcmp(argv[i],"--interp")&&i+1<argc) g_interp=atoi(argv[++i]);  /* 1=linear-interp playback (smooth ZOH staircase), 0=zero-order hold */
        else if (!strcmp(argv[i],"--solo")&&i+1<argc) g_solo=atoi(argv[++i]);   /* diag: mute all Paula channels except N (0-3) */
        else if (!strcmp(argv[i],"--mute")&&i+1<argc) g_mute_mask |= (1<<(atoi(argv[++i])&3));  /* diag: mute channel N, keep the rest */
        else if (!strcmp(argv[i],"--aud1lim")&&i+1<argc) g_aud1lim_thr=atoi(argv[++i]);  /* AUD1 attack-limiter threshold (de-tick); 0=off */
        else if (!strcmp(argv[i],"--aud1ratio")&&i+1<argc) { g_aud1lim_ratio=atoi(argv[++i]); if(g_aud1lim_ratio<1)g_aud1lim_ratio=1; }  /* limiter ratio */
        else if (!strcmp(argv[i],"--audstat")) g_audstat=1;                     /* diag: log per-channel vol/per changes */
        else if (!strcmp(argv[i],"--bltcpw")&&i+1<argc) g_blt_cyc_per_word=atoi(argv[++i]);  /* tune blitter busy cost (cycles/word) */
        else if (!strcmp(argv[i],"--dmasteal")&&i+1<argc) g_dma_steal_pct=atoi(argv[++i]);   /* tune uniform DMA cycle-steal (% extra clock/burst) */
        else if (!strcmp(argv[i],"--avdelay")&&i+1<argc) g_av_delay_frames=atoi(argv[++i]);  /* attract audio output delay in video frames (A/V sync; ear-tune; 0=minimal latency) */
        else if (!strcmp(argv[i],"--ftime")) g_ftime=1;
        /* save-state validation harness (headless): --savestate-at FRAME FILE writes a
         * quicksave at the start of FRAME then keeps running; --loadstate FILE thaws a
         * save at startup.  Both print a SAVETEST-HASH of RAM at the end so a save+reload
         * continuation can be proven bit-identical to an uninterrupted run. */
        else if (!strcmp(argv[i],"--savestate-at")&&i+2<argc) { savestate_at=atoi(argv[++i]); savestate_path=argv[++i]; }
        else if (!strcmp(argv[i],"--loadstate")&&i+1<argc) loadstate_path=argv[++i];
        /* --loadstate-at FRAME FILE: load mid-run (after the machine is warmed up by
         * running to FRAME) instead of at cold boot -- mirrors the live F9 path. */
        else if (!strcmp(argv[i],"--loadstate-at")&&i+2<argc) { loadstate_at=atoi(argv[++i]); loadstate_path=argv[++i]; }
        else if (!strcmp(argv[i],"--disasm")&&i+2<argc) { disasm_addr=(uint32_t)strtoul(argv[++i],0,0); disasm_len=atoi(argv[++i]); }
    }

    /* ---- resolve data paths (exe-relative `data/` for the distributable) ----
     * Pick the dataset directory: an explicit --dataset wins; otherwise prefer
     * the bundled <exedir>/data when it exists, then the dev tree.  The ADF dir
     * (--diskdir) and the loaded module default to the same place so a
     * double-clicked exe finds everything next to itself. */
    {
        static char datadir[1200];
        const char *dd = NULL;
        char cand[1200];
        if (dataset_set) {
            dd = g_dataset_buf;
        } else {
            snprintf(cand, sizeof(cand), "%s/data", g_exedir);
            if (dir_has_data(cand))                       dd = cand;
            else if (dir_has_data("../portable/moonstone_hdd")) dd = "../portable/moonstone_hdd";
            else if (dir_has_data("data"))                dd = "data";
            else                                          dd = "../portable/moonstone_hdd";
            snprintf(g_dataset_buf, sizeof(g_dataset_buf), "%s", dd);
        }
        snprintf(datadir, sizeof(datadir), "%s", g_dataset_buf);
        if (!diskdir_set) g_diskdir = datadir;            /* ADFs live alongside the dataset */
        /* default module: the loader `nb` under --os, else the cracktro `crystal` */
        if (modarg) { mod = modarg; }
        else {
            static char modbuf[1300];
            snprintf(modbuf, sizeof(modbuf), "%s/%s", datadir, g_os ? "nb" : "crystal");
            mod = modbuf;
        }
    }

    /* Default the log next to the EXE (always writable + findable), so a windowed
     * crash leaves 'moonstone.log' beside the game instead of vanishing silently. */
    char logbuf[1200];
    if (!logpath) { snprintf(logbuf, sizeof(logbuf), "%s/moonstone.log", g_exedir[0] ? g_exedir : "."); logpath = logbuf; }
    g_log = fopen(logpath, "w");
    if (!g_log) g_log = stderr;
#ifdef _WIN32
    SetUnhandledExceptionFilter(moon_crash_filter);  /* self-report host faults */
#endif
    fprintf(g_log, "%s\n", MOON_ATTRIB);
    fprintf(g_log, "build: %s\n", MOON_BUILD);
    fprintf(g_log, "exedir=%s dataset=%s diskdir=%s mod=%s\n", g_exedir, g_dataset, g_diskdir, mod);

    /* Convenience: if the player supplied only the three .adf disk images, pull
     * the four boot modules (nb/program/mog/crystal) out of Disk 1's filesystem
     * so they don't have to extract them by hand.  No-op once they exist. */
    ensure_boot_modules(g_dataset, g_diskdir);

    /* --wav: headless capture of the generated Paula mix (enables audio gen on
     * the non-SDL path).  A placeholder 44-byte header is written now and
     * patched with the real data length on close. */
    if (wavpath) {
        g_wav = fopen(wavpath, "wb");
        if (g_wav) { wav_write_header(g_wav, 0); g_audio_on = 1; }
        else fprintf(stderr, "could not open WAV %s\n", wavpath);
    }

    Module m;
    if (load_hunk(g_ram, RAM_SIZE, mod, base, 8, &m) != 0) {
        fprintf(stderr, "failed to load %s\n", mod); return 1;
    }
    fprintf(g_log, "Loaded %s: %d segments, entry=%06x end=%06x\n", m.name, m.nseg, m.entry, m.end);
    for (int i=0;i<m.nseg;i++)
        fprintf(g_log, "  seg[%d] kind=%d base=%06x size=%06x\n", i, m.seg[i].kind, m.seg[i].base, m.seg[i].size);

    /* stack near top of RAM, below custom space */
    uint32_t sp = 0x80000;
    setup_amigaos(sp);
    if (g_os) disk_load_adfs();   /* trackdisk DMA reads served from these */
    /* Blitter busy-time model active for the attract intro only (g_os path);
     * disabled at Mog launch (see moon_instr_hook) and never on for the cracktro. */
    g_blt_busy_scope = g_os;

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_PC, m.entry);
    m68k_set_reg(M68K_REG_ISP, 0x0007f000);   /* supervisor stack (for traps/IRQ) */
    m68k_set_reg(M68K_REG_USP, sp);
    m68k_set_reg(M68K_REG_SR, sr);
    m68k_set_reg(M68K_REG_SP, sp);
    g_trace = trace_n;

    fprintf(g_log, "--- run: pc=%06x sp=%06x sr=%04x frames=%d sdl=%d ---\n", m.entry, sp, sr, frames, sdl);
    (void)steps;
    /* Cold-boot load: thaw a save BEFORE entering either run loop.  Was headless-only
     * (the sdl dispatch came first, silently ignoring --loadstate in windowed mode);
     * shared now so a live window can boot straight into a save -- e.g. straight into
     * a mid-combat repro without hand-navigating the menus.  The warmed variant
     * (--loadstate-at FRAME) stays headless-only. */
    if (loadstate_path && loadstate_at < 0) {
        if (!load_state(loadstate_path)) { fprintf(stderr, "loadstate failed: %s\n", loadstate_path); return 1; }
        fprintf(g_log, "--- loadstate %s -> ic=%llu pc=%06x ---\n",
                loadstate_path, (unsigned long long)g_icount, (unsigned)m68k_get_reg(NULL,M68K_REG_PC));
    }
    if (sdl) { int rc = run_sdl(scale); if (g_log!=stderr) fclose(g_log); return rc; }
    LARGE_INTEGER pf; QueryPerformanceFrequency(&pf);
    double pf_ms = 1000.0 / (double)pf.QuadPart;
    double emu_max=0, emu_sum=0, ren_max=0, ren_sum=0; int ftn=0;
    for (int fr = 0; fr < frames && !g_stop; fr++) {
        g_cur_frame = fr;
        if (savestate_path && fr == savestate_at) {   /* freeze at this boundary (gated like the live UI) */
            if (load_in_progress()) fprintf(g_log, "SAVE-GATED at fr %d: load in progress\n", fr);
            else save_state(savestate_path);
        }
        if (loadstate_path && fr == loadstate_at) {   /* WARMED-UP load: mirrors the live F9 path */
            if (!load_state(loadstate_path)) { fprintf(stderr, "loadstate failed\n"); return 1; }
            fprintf(g_log, "--- loadstate-at fr %d: %s -> ic=%llu pc=%06x ---\n",
                    fr, loadstate_path, (unsigned long long)g_icount, (unsigned)m68k_get_reg(NULL,M68K_REG_PC));
        }
        if (g_ndiskev) apply_diskat(fr);
        if (g_nscript) apply_script(fr);
        if (g_ntypeev) apply_type(fr);   /* scripted typed text (--type) for headless name-entry validation */
        autoswap_tick();   /* seamless disk swap: auto-confirm any "insert disk" prompt */
        if (press_frame >= 0 && fr >= press_frame) { g_fire = 1; g_fire2 = 1; }
        if (clickthru) { g_fire = g_fire2 = ((fr % 50) < 10) ? 1 : 0; } /* pulse fire */
        /* run one PAL frame in small bursts so the beam advances finely */
        LARGE_INTEGER t0,t1,t2; if (g_ftime) QueryPerformanceCounter(&t0);
        g_frame_cycle = 0;
        int zeroburst = 0;
        while (g_frame_cycle < CYCLES_PER_FRAME && !g_stop) {
            update_ipl();
            int did = m68k_execute(160);
            if (did <= 0) { if (++zeroburst >= 8) { halt("cpu halted (double bus fault / runaway)"); break; } }
            else zeroburst = 0;
            /* Uniform DMA cycle-steal (attract-scoped): see g_dma_steal_pct. */
            int elapsed = did;
            if (g_blt_busy_scope && g_dma_steal_pct > 0)
                elapsed += did * g_dma_steal_pct / 100;
            g_frame_cycle += elapsed; g_cycles += elapsed;
            beam_update(); cia_tick(elapsed);
            /* interleaved Paula audio: service mid-frame AUDx IRQs faithfully
             * (see audio_advance) -- one-shot SFX no longer auto-loop a 2nd pass */
            audio_advance((int)((int64_t)SMP_PER_FRAME * g_frame_cycle / CYCLES_PER_FRAME));
        }
        if (g_ftime) QueryPerformanceCounter(&t1);
        /* vertical blank: capture the (now-stable) displayed frame BEFORE the
         * next frame's foreground starts rebuilding the copper list, then raise
         * VERTB (bit5) so the game's L3 handler runs at the start of vblank. */
        capture_frame();
        if (g_ftime) { QueryPerformanceCounter(&t2);
            double e=(t1.QuadPart-t0.QuadPart)*pf_ms, r=(t2.QuadPart-t1.QuadPart)*pf_ms;
            emu_sum+=e; ren_sum+=r; if(e>emu_max)emu_max=e; if(r>ren_max)ren_max=r; ftn++;
        }
        if (vertb_gate()) g_custom[0x09c>>1] |= 0x0020;
        update_ipl();
        audio_flush();   /* finish + generate this frame's Paula audio (WAV capture etc.) */

        if (dumpevery && dumppath && (fr % dumpevery)==0) {
            char p[256]; snprintf(p,sizeof(p),"%s_%04d.ppm",dumppath,fr);
            dump_captured(p);
            if (g_rdbg && g_log)
                fprintf(g_log,"DUMPFR fr=%d ic=%llu palette-distinct=%d dispbase=%06x\n",
                        fr,(unsigned long long)g_icount,palette_distinct_nonzero(),(unsigned)g_disp_base);
        }
    }

    if (savestate_path || loadstate_path) {   /* save-state self-test: hash RAM so save+reload can be proven identical */
        uint32_t h = 2166136261u;             /* FNV-1a over all 2MB of RAM */
        for (uint32_t i = 0; i < RAM_SIZE; i++) { h ^= g_ram[i]; h *= 16777619u; }
        printf("SAVETEST-HASH ram_fnv=%08x ic=%llu pc=%06x\n",
               h, (unsigned long long)g_icount, (unsigned)m68k_get_reg(NULL,M68K_REG_PC));
        if (g_log) fprintf(g_log, "SAVETEST-HASH ram_fnv=%08x ic=%llu pc=%06x\n",
               h, (unsigned long long)g_icount, (unsigned)m68k_get_reg(NULL,M68K_REG_PC));
    }

    if (g_ftime && ftn) {
        printf("ftime: frames=%d emu_avg=%.3fms emu_max=%.3fms render_avg=%.3fms render_max=%.3fms total_avg=%.3fms (budget 20ms)\n",
               ftn, emu_sum/ftn, emu_max, ren_sum/ftn, ren_max, (emu_sum+ren_sum)/ftn);
        if (g_log) fprintf(g_log, "ftime: frames=%d emu_avg=%.3fms emu_max=%.3fms render_avg=%.3fms render_max=%.3fms total_avg=%.3fms\n",
               ftn, emu_sum/ftn, emu_max, ren_sum/ftn, ren_max, (emu_sum+ren_sum)/ftn);
    }
    if (dumppath) { dump_captured(dumppath); printf("dumped frame -> %s\n", dumppath); }
    if (ramdump) { FILE *rf=fopen(ramdump,"wb"); if(rf){fwrite(g_ram,1,RAM_SIZE,rf);fclose(rf);printf("dumped ram -> %s\n",ramdump);} }
    if (disasm_len > 0) {   /* dev: disassemble a 68k code region from live RAM */
        char dis[256];
        for (uint32_t a = disasm_addr; a < disasm_addr + (uint32_t)disasm_len; ) {
            int n = m68k_disassemble(dis, a, M68K_CPU_TYPE_68000);
            printf("%06x: %s\n", a, dis);
            a += (n > 0 ? (uint32_t)n : 2);
        }
    }

    /* finalize WAV + report audio mix statistics (validation) */
    if (g_wav) {
        fseek(g_wav, 0, SEEK_SET); wav_write_header(g_wav, g_wav_bytes); fclose(g_wav); g_wav = NULL;
    }
    if (g_audio_on && g_a_total) {
        double rms = (g_a_total ? (g_a_sumsq / (double)g_a_total) : 0.0);
        rms = (rms > 0 ? __builtin_sqrt(rms) : 0.0);
        double pct_nz = 100.0 * (double)g_a_nonzero / (double)g_a_total;
        fprintf(g_log, "--- audio: samples=%llu min=%d max=%d rms=%.1f nonzero=%.1f%% "
                "irq{a0=%llu a1=%llu a2=%llu a3=%llu} ---\n",
                (unsigned long long)g_a_total, g_a_min, g_a_max, rms, pct_nz,
                (unsigned long long)g_aud_irq[0], (unsigned long long)g_aud_irq[1],
                (unsigned long long)g_aud_irq[2], (unsigned long long)g_aud_irq[3]);
        printf("audio: samples=%llu min=%d max=%d rms=%.1f nonzero=%.1f%% irq=%llu/%llu/%llu/%llu%s\n",
               (unsigned long long)g_a_total, g_a_min, g_a_max, rms, pct_nz,
               (unsigned long long)g_aud_irq[0], (unsigned long long)g_aud_irq[1],
               (unsigned long long)g_aud_irq[2], (unsigned long long)g_aud_irq[3],
               wavpath ? "" : "");
    }

    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
    fprintf(g_log, "--- stopped: %s ---\n", g_stop_reason);
    fprintf(g_log, "  pc=%06x sr=%04x icount=%llu unmapped=%u\n",
            pc, (unsigned)m68k_get_reg(NULL,M68K_REG_SR), (unsigned long long)g_icount, g_unmapped);
    fprintf(g_log, "  custom writes logged=%u, cia w=%u r=%u\n", g_custw_log, g_ciaw_log, g_ciar_log);
    if (g_msleak_blocked)
        fprintf(g_log, "  Moonstone enemy-leak guards fired=%d (enemy/Guardian Moonstone assignments blocked)\n", g_msleak_blocked);
    if (g_log != stderr) fclose(g_log);

    printf("done: stop=%s pc=%06x icount=%llu unmapped=%u (see %s)\n",
           g_stop_reason, pc, (unsigned long long)g_icount, g_unmapped, logpath);
    return 0;
}
