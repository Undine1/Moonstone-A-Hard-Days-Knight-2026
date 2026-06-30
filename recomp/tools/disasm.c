/* Offline 68k disassembler for Moonstone savestates (uses the vendored Musashi m68kdasm).
 * Build:  zig cc -O2 tools/disasm.c vendor/Musashi-master/m68kdasm.c -Ivendor/Musashi-master -o build/disasm.exe
 * Usage:  disasm.exe <save.sav> <startHex> <count>
 * Reads g_ram from the save (RAM at file offset 20+nregs*4; nregs is a LE u32 at offset 16). */
#include <stdio.h>
#include <stdlib.h>
#include "m68k.h"

static unsigned char *g_ram_p;
static unsigned       g_ram_sz = 0x200000u;

unsigned int m68k_read_disassembler_8 (unsigned int a){ return g_ram_p[a & (g_ram_sz-1)]; }
unsigned int m68k_read_disassembler_16(unsigned int a){ a&=(g_ram_sz-1); return ((unsigned)g_ram_p[a]<<8)|g_ram_p[a+1]; }
unsigned int m68k_read_disassembler_32(unsigned int a){ a&=(g_ram_sz-1);
    return ((unsigned)g_ram_p[a]<<24)|((unsigned)g_ram_p[a+1]<<16)|((unsigned)g_ram_p[a+2]<<8)|g_ram_p[a+3]; }

int main(int argc, char **argv){
    if (argc < 4){ fprintf(stderr,"usage: %s <save.sav> <startHex> <count>\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1],"rb"); if(!f){ perror("open"); return 1; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char *d = (unsigned char*)malloc(sz);
    if (fread(d,1,sz,f)!=(size_t)sz){ fprintf(stderr,"short read\n"); return 1; } fclose(f);
    unsigned nregs = (unsigned)d[16] | ((unsigned)d[17]<<8) | ((unsigned)d[18]<<16) | ((unsigned)d[19]<<24);
    g_ram_p = d + 20 + nregs*4;
    unsigned pc = (unsigned)strtoul(argv[2],0,16);
    int count = atoi(argv[3]);
    char buf[256];
    for (int i=0;i<count;i++){
        unsigned s = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
        printf("%06x: %s\n", pc, buf);
        pc += (s ? s : 2);
    }
    return 0;
}
