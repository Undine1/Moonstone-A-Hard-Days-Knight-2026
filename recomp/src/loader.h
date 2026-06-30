/* Moonstone Reborn — AmigaDOS Hunk loader (part of github.com/Undine1/moonstone-reborn)
 * Copyright (C) 2026 Undine1 <github.com/Undine1>.  GNU GPL v3 — see LICENSE. */
#ifndef MOON_LOADER_H
#define MOON_LOADER_H
#include <stdint.h>

#define HUNK_CODE        1001
#define HUNK_DATA        1002
#define HUNK_BSS         1003
#define HUNK_RELOC32     1004
#define HUNK_SYMBOL      1008
#define HUNK_DEBUG       1009
#define HUNK_END         1010
#define HUNK_HEADER      1011
#define HUNK_RELOC32SHORT 1020

typedef struct { uint32_t base; uint32_t size; int kind; } SegInfo;

typedef struct {
    char     name[64];
    uint32_t load_base;   /* base of first segment */
    uint32_t entry;       /* entry point (first segment base) */
    uint32_t end;         /* next free linear address after module */
    int      nseg;
    SegInfo  seg[96];
} Module;

/* Load an AmigaDOS Hunk (LoadSeg) executable into ram[] starting at `base`,
 * applying 32-bit relocations. Returns 0 on success, negative on error. */
int load_hunk(uint8_t *ram, uint32_t ram_size, const char *path,
              uint32_t base, uint32_t align, Module *out);

#endif
