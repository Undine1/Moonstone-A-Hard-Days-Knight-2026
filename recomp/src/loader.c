/* Moonstone 2026 - AmigaDOS Hunk loader (part of github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026)
 * Copyright (C) 2026 Undine1 <github.com/Undine1>.  GNU GPL v3 - see LICENSE. */
#include "loader.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t src, tgt; } Rel;

static uint32_t rd32(const uint8_t *d, size_t o) {
    return ((uint32_t)d[o] << 24) | ((uint32_t)d[o+1] << 16) |
           ((uint32_t)d[o+2] << 8) | (uint32_t)d[o+3];
}
static void wr32(uint8_t *d, size_t o, uint32_t v) {
    d[o]=(uint8_t)(v>>24); d[o+1]=(uint8_t)(v>>16);
    d[o+2]=(uint8_t)(v>>8); d[o+3]=(uint8_t)v;
}
static int take_u32(const uint8_t *d, size_t len, size_t *o, uint32_t *v) {
    if (*o > len || len - *o < 4u) return 0;
    *v = rd32(d, *o);
    *o += 4u;
    return 1;
}
static int take_u16(const uint8_t *d, size_t len, size_t *o, uint16_t *v) {
    if (*o > len || len - *o < 2u) return 0;
    *v = (uint16_t)(((uint16_t)d[*o] << 8) | d[*o + 1u]);
    *o += 2u;
    return 1;
}
static int skip_bytes(size_t len, size_t *o, size_t n) {
    if (*o > len || n > len - *o) return 0;
    *o += n;
    return 1;
}
static int skip_words(size_t len, size_t *o, uint32_t words) {
    if ((size_t)words > SIZE_MAX / 4u) return 0;
    return skip_bytes(len, o, (size_t)words * 4u);
}
static int words_to_u32_bytes(uint32_t words, uint32_t *bytes) {
    uint64_t n = (uint64_t)words * 4u;
    if (n > UINT32_MAX) return 0;
    *bytes = (uint32_t)n;
    return 1;
}
static int append_rel(Rel **rels, size_t *nrel, size_t *caprel,
                      uint32_t src, uint32_t tgt) {
    if (*nrel == *caprel) {
        size_t cap = *caprel ? *caprel * 2u : 256u;
        if (cap < *caprel || cap > SIZE_MAX / sizeof(Rel)) return 0;
        Rel *grown = (Rel*)realloc(*rels, cap * sizeof(Rel));
        if (!grown) return 0;
        *rels = grown;
        *caprel = cap;
    }
    (*rels)[*nrel].src = src;
    (*rels)[*nrel].tgt = tgt;
    (*nrel)++;
    return 1;
}

int load_hunk(uint8_t *ram, uint32_t ram_size, const char *path,
              uint32_t base, uint32_t align, Module *out) {
    FILE *f = NULL;
    uint8_t *d = NULL;
    Rel *rels = NULL;
    size_t nrel = 0, caprel = 0;
    int rc = -2;

#define HUNK_FAIL(code, ...) do { \
    fprintf(stderr, "%s: ", path ? path : "load_hunk"); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); \
    rc = (code); goto done; \
} while (0)

    if (!ram || !out || !path || !*path || !align || (align & (align - 1u)) != 0u)
        HUNK_FAIL(-1, "invalid loader arguments");
    if (base > ram_size) HUNK_FAIL(-4, "base 0x%x exceeds RAM size 0x%x", base, ram_size);

    f = fopen(path, "rb");
    if (!f) HUNK_FAIL(-1, "cannot open");
    if (fseek(f, 0, SEEK_END) != 0) HUNK_FAIL(-2, "cannot seek to end");
    long fsz_long = ftell(f);
    if (fsz_long < 0) HUNK_FAIL(-2, "cannot determine file size");
    if (fseek(f, 0, SEEK_SET) != 0) HUNK_FAIL(-2, "cannot rewind");
    size_t fsz = (size_t)fsz_long;
    d = (uint8_t*)malloc(fsz ? fsz : 1u);
    if (!d) HUNK_FAIL(-2, "out of memory reading %zu bytes", fsz);
    if (fread(d, 1, fsz, f) != fsz || ferror(f)) HUNK_FAIL(-2, "short read");
    if (fclose(f) != 0) { f = NULL; HUNK_FAIL(-2, "close failed after read"); }
    f = NULL;

    size_t o = 0;
    uint32_t word = 0;
    if (!take_u32(d, fsz, &o, &word) || word != HUNK_HEADER)
        HUNK_FAIL(-3, "not HUNK_HEADER");

    /* Resident-library name list (usually empty). */
    for (;;) {
        uint32_t words;
        if (!take_u32(d, fsz, &o, &words)) HUNK_FAIL(-3, "truncated resident-name list");
        if (!words) break;
        if (!skip_words(fsz, &o, words)) HUNK_FAIL(-3, "resident name exceeds file");
    }

    uint32_t table_size, first, last;
    if (!take_u32(d, fsz, &o, &table_size) ||
        !take_u32(d, fsz, &o, &first) ||
        !take_u32(d, fsz, &o, &last))
        HUNK_FAIL(-3, "truncated hunk table header");
    if (!table_size || table_size > 96u || first > last ||
        (uint64_t)last - first + 1u != table_size)
        HUNK_FAIL(-3, "invalid hunk table range first=%u last=%u count=%u",
                  first, last, table_size);
    for (uint32_t i = 0; i < table_size; i++)
        if (!take_u32(d, fsz, &o, &word)) HUNK_FAIL(-3, "truncated hunk size table");

    uint32_t seg_base[96], seg_size[96];
    int seg_kind[96];
    size_t seg_data_off[96];
    uint32_t addr = base;
    int nseg = 0, cur = -1;

    while (o < fsz) {
        size_t block_off = o;
        uint32_t raw;
        if (!take_u32(d, fsz, &o, &raw)) HUNK_FAIL(-3, "truncated block tag at %zu", block_off);
        uint32_t ht = raw & 0x0fffffffu;

        if (ht == HUNK_CODE || ht == HUNK_DATA || ht == HUNK_BSS) {
            uint32_t words, nbytes;
            if ((uint32_t)nseg >= table_size || nseg >= 96)
                HUNK_FAIL(-3, "more segments than declared at %zu", block_off);
            if (!take_u32(d, fsz, &o, &words) || !words_to_u32_bytes(words, &nbytes))
                HUNK_FAIL(-3, "invalid segment size at %zu", block_off);
            size_t data_off = SIZE_MAX;
            if (ht != HUNK_BSS) {
                data_off = o;
                if (!skip_bytes(fsz, &o, (size_t)nbytes))
                    HUNK_FAIL(-3, "segment data exceeds file at %zu", block_off);
            }
            if (addr > ram_size || nbytes > ram_size - addr)
                HUNK_FAIL(-4, "segment at 0x%x size 0x%x exceeds RAM", addr, nbytes);

            seg_base[nseg] = addr;
            seg_size[nseg] = nbytes;
            seg_kind[nseg] = (int)ht;
            seg_data_off[nseg] = data_off;
            cur = nseg++;

            uint64_t adv = ((uint64_t)nbytes + align - 1u) & ~((uint64_t)align - 1u);
            if (adv > (uint64_t)ram_size - addr)
                HUNK_FAIL(-4, "aligned segment at 0x%x exceeds RAM", addr);
            addr += (uint32_t)adv;
        } else if (ht == HUNK_RELOC32) {
            if (cur < 0) HUNK_FAIL(-3, "RELOC32 outside a segment at %zu", block_off);
            for (;;) {
                uint32_t count, target;
                if (!take_u32(d, fsz, &o, &count)) HUNK_FAIL(-3, "truncated RELOC32 count");
                if (!count) break;
                if (!take_u32(d, fsz, &o, &target)) HUNK_FAIL(-3, "truncated RELOC32 target");
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t so;
                    if (!take_u32(d, fsz, &o, &so)) HUNK_FAIL(-3, "truncated RELOC32 offsets");
                    if (seg_size[cur] < 4u || so > seg_size[cur] - 4u)
                        HUNK_FAIL(-3, "RELOC32 source 0x%x outside segment %d", so, cur);
                    if (!append_rel(&rels, &nrel, &caprel, seg_base[cur] + so, target))
                        HUNK_FAIL(-2, "out of memory collecting relocations");
                }
            }
        } else if (ht == HUNK_RELOC32SHORT) {
            if (cur < 0) HUNK_FAIL(-3, "RELOC32SHORT outside a segment at %zu", block_off);
            for (;;) {
                uint16_t count16, target16;
                if (!take_u16(d, fsz, &o, &count16)) HUNK_FAIL(-3, "truncated RELOC32SHORT count");
                if (!count16) {
                    if ((o & 3u) && !skip_bytes(fsz, &o, 2u))
                        HUNK_FAIL(-3, "truncated RELOC32SHORT padding");
                    break;
                }
                if (!take_u16(d, fsz, &o, &target16)) HUNK_FAIL(-3, "truncated RELOC32SHORT target");
                for (uint32_t i = 0; i < count16; i++) {
                    uint16_t so16;
                    if (!take_u16(d, fsz, &o, &so16)) HUNK_FAIL(-3, "truncated RELOC32SHORT offsets");
                    uint32_t so = so16;
                    if (seg_size[cur] < 4u || so > seg_size[cur] - 4u)
                        HUNK_FAIL(-3, "RELOC32SHORT source 0x%x outside segment %d", so, cur);
                    if (!append_rel(&rels, &nrel, &caprel, seg_base[cur] + so, target16))
                        HUNK_FAIL(-2, "out of memory collecting relocations");
                }
                /* Preserve the loader's established per-group longword alignment. */
                if ((o & 3u) && !skip_bytes(fsz, &o, 2u))
                    HUNK_FAIL(-3, "truncated RELOC32SHORT group padding");
            }
        } else if (ht == HUNK_SYMBOL) {
            for (;;) {
                uint32_t words, value;
                if (!take_u32(d, fsz, &o, &words)) HUNK_FAIL(-3, "truncated SYMBOL name length");
                if (!words) break;
                if (!skip_words(fsz, &o, words) || !take_u32(d, fsz, &o, &value))
                    HUNK_FAIL(-3, "SYMBOL entry exceeds file");
            }
        } else if (ht == HUNK_DEBUG) {
            uint32_t words;
            if (!take_u32(d, fsz, &o, &words) || !skip_words(fsz, &o, words))
                HUNK_FAIL(-3, "DEBUG block exceeds file");
        } else if (ht == HUNK_END) {
            cur = -1;
        } else {
            HUNK_FAIL(-5, "unknown hunk block %u at %zu", ht, block_off);
        }
    }

    if ((uint32_t)nseg != table_size)
        HUNK_FAIL(-3, "declared %u segments but parsed %d", table_size, nseg);
    if (cur >= 0) HUNK_FAIL(-3, "final segment is missing HUNK_END");
    for (size_t i = 0; i < nrel; i++) {
        if (rels[i].tgt < first || rels[i].tgt > last)
            HUNK_FAIL(-3, "relocation target %u outside [%u,%u]", rels[i].tgt, first, last);
    }

    /* Nothing above mutates guest RAM.  Commit only after the complete file and
     * every segment/relocation have passed their bounds checks. */
    for (int i = 0; i < nseg; i++) {
        if (seg_kind[i] == HUNK_BSS) memset(ram + seg_base[i], 0, seg_size[i]);
        else memcpy(ram + seg_base[i], d + seg_data_off[i], seg_size[i]);
    }
    for (size_t i = 0; i < nrel; i++) {
        uint32_t target_index = rels[i].tgt - first;
        uint32_t addend = rd32(ram, rels[i].src);
        wr32(ram, rels[i].src, addend + seg_base[target_index]);
    }

    Module result;
    memset(&result, 0, sizeof(result));
    const char *name = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') name = p + 1;
    snprintf(result.name, sizeof(result.name), "%s", name);
    result.load_base = nseg ? seg_base[0] : base;
    result.entry = result.load_base;
    result.end = addr;
    result.nseg = nseg;
    for (int i = 0; i < nseg; i++) {
        result.seg[i].base = seg_base[i];
        result.seg[i].size = seg_size[i];
        result.seg[i].kind = seg_kind[i];
    }
    *out = result;
    rc = 0;

done:
    if (f) fclose(f);
    free(d);
    free(rels);
    return rc;
#undef HUNK_FAIL
}
