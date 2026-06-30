/* Moonstone Reborn — AmigaDOS Hunk loader (part of github.com/Undine1/moonstone-reborn)
 * Copyright (C) 2026 Undine1 <github.com/Undine1>.  GNU GPL v3 — see LICENSE. */
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *d, size_t o) {
    return ((uint32_t)d[o] << 24) | ((uint32_t)d[o+1] << 16) |
           ((uint32_t)d[o+2] << 8) | (uint32_t)d[o+3];
}
static uint16_t rd16(const uint8_t *d, size_t o) {
    return (uint16_t)((d[o] << 8) | d[o+1]);
}
static void wr32(uint8_t *d, size_t o, uint32_t v) {
    d[o]=(uint8_t)(v>>24); d[o+1]=(uint8_t)(v>>16); d[o+2]=(uint8_t)(v>>8); d[o+3]=(uint8_t)v;
}

int load_hunk(uint8_t *ram, uint32_t ram_size, const char *path,
              uint32_t base, uint32_t align, Module *out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "load_hunk: cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = (uint8_t*)malloc(fsz);
    if (fread(d, 1, fsz, f) != (size_t)fsz) { fclose(f); free(d); return -2; }
    fclose(f);

    memset(out, 0, sizeof(*out));
    const char *nm = strrchr(path, '/'); const char *nm2 = strrchr(path, '\\');
    if (nm2 > nm) nm = nm2;
    snprintf(out->name, sizeof(out->name), "%s", nm ? nm+1 : path);

    size_t o = 0;
    if (rd32(d, o) != HUNK_HEADER) { free(d); fprintf(stderr,"%s: not HUNK_HEADER\n",path); return -3; }
    o += 4;
    /* resident library name list (usually empty) */
    for (;;) { uint32_t n = rd32(d,o); o += 4; if (n==0) break; o += n*4; }
    uint32_t table_size = rd32(d,o); o += 4;
    o += 8; /* first, last */
    /* sizes table */
    uint32_t *sizes = (uint32_t*)malloc(table_size*sizeof(uint32_t));
    for (uint32_t i=0;i<table_size;i++){ sizes[i]=rd32(d,o); o+=4; }

    /* first pass-ish: stream blocks, allocating bases as we meet CODE/DATA/BSS */
    uint32_t addr = base;
    int nseg = 0;
    /* store per-seg base + file data offset for reloc pass */
    uint32_t seg_base[96]; uint32_t seg_size[96]; int seg_kind[96];
    /* relocations: collect (src_off_linear, target_seg) */
    typedef struct { uint32_t src; uint32_t tgt; } Rel;
    Rel *rels = NULL; size_t nrel=0, caprel=0;
    int cur = -1;

    while (o + 4 <= (size_t)fsz) {
        uint32_t ht = rd32(d,o) & 0x0fffffff; o += 4;
        if (ht == HUNK_CODE || ht == HUNK_DATA) {
            uint32_t n = rd32(d,o); o += 4; uint32_t nbytes = n*4;
            uint32_t a = addr;
            if (a + nbytes > ram_size) { fprintf(stderr,"seg exceeds ram\n"); free(d);free(sizes);free(rels);return -4; }
            memcpy(ram + a, d + o, nbytes);
            seg_base[nseg]=a; seg_size[nseg]=nbytes; seg_kind[nseg]=(int)ht;
            cur = nseg; nseg++;
            o += nbytes;
            uint32_t adv = (nbytes + align - 1) & ~(align - 1);
            addr += adv;
        } else if (ht == HUNK_BSS) {
            uint32_t n = rd32(d,o); o += 4; uint32_t nbytes = n*4;
            uint32_t a = addr;
            if (a + nbytes > ram_size) { fprintf(stderr,"bss exceeds ram\n"); free(d);free(sizes);free(rels);return -4; }
            memset(ram + a, 0, nbytes);
            seg_base[nseg]=a; seg_size[nseg]=nbytes; seg_kind[nseg]=(int)ht;
            cur = nseg; nseg++;
            uint32_t adv = (nbytes + align - 1) & ~(align - 1);
            addr += adv;
        } else if (ht == HUNK_RELOC32) {
            for (;;) {
                uint32_t cnt = rd32(d,o); o += 4; if (cnt==0) break;
                uint32_t tgt = rd32(d,o); o += 4;
                for (uint32_t i=0;i<cnt;i++){
                    uint32_t so = rd32(d,o); o += 4;
                    if (nrel==caprel){ caprel=caprel?caprel*2:1024; rels=(Rel*)realloc(rels,caprel*sizeof(Rel)); }
                    rels[nrel].src = seg_base[cur] + so; rels[nrel].tgt = tgt; nrel++;
                }
            }
        } else if (ht == HUNK_RELOC32SHORT) {
            for (;;) {
                uint32_t cnt = rd16(d,o); o += 2;
                if (cnt==0){ if (o & 3) o += 2; break; }
                uint32_t tgt = rd16(d,o); o += 2;
                for (uint32_t i=0;i<cnt;i++){
                    uint32_t so = rd16(d,o); o += 2;
                    if (nrel==caprel){ caprel=caprel?caprel*2:1024; rels=(Rel*)realloc(rels,caprel*sizeof(Rel)); }
                    rels[nrel].src = seg_base[cur] + so; rels[nrel].tgt = tgt; nrel++;
                }
                if (o & 3) o += 2;
            }
        } else if (ht == HUNK_SYMBOL) {
            for (;;){ uint32_t n=rd32(d,o); o+=4; if(n==0)break; o += n*4 + 4; }
        } else if (ht == HUNK_DEBUG) {
            uint32_t n = rd32(d,o); o += 4 + n*4;
        } else if (ht == HUNK_END) {
            cur = -1;
        } else {
            fprintf(stderr,"%s: unknown hunk block %u at %zu\n", path, ht, o-4);
            free(d);free(sizes);free(rels); return -5;
        }
    }

    /* apply relocations: *(src) += seg_base[tgt] */
    for (size_t i=0;i<nrel;i++){
        if (rels[i].tgt >= (uint32_t)nseg) continue;
        uint32_t addend = rd32(ram, rels[i].src);
        wr32(ram, rels[i].src, addend + seg_base[rels[i].tgt]);
    }

    out->load_base = nseg ? seg_base[0] : base;
    out->entry     = nseg ? seg_base[0] : base;
    out->end       = addr;
    out->nseg      = nseg;
    for (int i=0;i<nseg && i<96;i++){ out->seg[i].base=seg_base[i]; out->seg[i].size=seg_size[i]; out->seg[i].kind=seg_kind[i]; }

    free(d); free(sizes); free(rels);
    return 0;
}
