/* Test-only retail comparisons, save lifecycle and encounter observations.
 * Never deploy. Every invocation requires an explicit scratch --log. */
#define moon_instr_hook lair_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int lair_trace;
static unsigned lair_snapshot_pc, lair_stop_pc;
static const char *lair_snapshot_path;

void moon_instr_hook(unsigned pc) {
    if (lair_snapshot_path && pc == lair_snapshot_pc) {
        assert(save_state(lair_snapshot_path));
        fprintf(g_log, "LAIR-SNAPSHOT pc=%06x\n", pc);
        lair_snapshot_path = NULL;
    }
    if (lair_trace && (pc == 0x21ca4 || pc == 0x259f8 || pc == 0x2173c
        || pc == 0x21852 || pc == 0x21cf4 || pc == 0x21cfa || pc == 0x21d00
        || pc == 0x21d22 || pc == 0x21d1e || pc == 0x24ec6)) {
        unsigned node = r32(0x37178), actor = r32(0x2ebd0);
        int index = lair_xp_index(node);
        fprintf(g_log, "LAIR-STEP pc=%06x fr=%d node=%06x index=%d count=%d active=%d max=%d actor=%06x xp=%u claimed=%d gold=%u xy=%08x\n",
                pc, g_cur_frame, node, index, (int16_t)r16(0x2e1e4), r16(0x2e1e8), r16(0x2e1e6),
                actor, actor < RAM_SIZE-0x84 ? r16(actor+0x4e) : 0,
                index < 0 ? -1 : g_lair_xp_awarded[index],
                node < RAM_SIZE-0x14 ? r16(node+8) : 0,
                node < RAM_SIZE-0x14 ? r32(node+10) : 0);
    }
    if (lair_stop_pc && pc == lair_stop_pc) {
        fprintf(g_log, "LAIR-STOP pc=%06x\n", pc);
        w16(0x1ef000, 0x60fe);
        m68k_set_reg(M68K_REG_SR, 0x2700);
        m68k_set_reg(M68K_REG_PC, 0x1ef000);
        lair_stop_pc = 0;
        return;
    }
    lair_original_hook(pc);
}

#define XP_ACTOR 0x100000u
#define XP_NODES 0x100800u
#define XP_ITEMS 0x101000u
#define XP_STOP 0x1ef000u
#define XP_STACK 0x1ff000u

static void lair_finish(unsigned stop) {
    unsigned n = 0;
    while (m68k_get_reg(NULL, M68K_REG_PC) != stop && n++ < 200000) m68k_execute(1);
    if (m68k_get_reg(NULL, M68K_REG_PC) != stop)
        fprintf(stderr, "lair_finish pc=%06x wanted=%06x\n", m68k_get_reg(NULL,M68K_REG_PC),stop);
    assert(m68k_get_reg(NULL, M68K_REG_PC) == stop);
}

static void lair_load(const char *path) { assert(load_state(path)); }

static uint8_t *lair_machine(size_t *size) {
    uint32_t regs[SAVE_NREGS];
    for (int i=0;i<SAVE_NREGS;i++) regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
    *size=sv_payload_size(SAVE_VERSION);
    uint8_t *bytes=malloc(*size);
    SvCursor cursor={bytes,*size,0,1};
    assert(bytes && sv_serialize(&cursor,regs,SAVE_NREGS,SAVE_VERSION));
    return bytes;
}

static unsigned lair_prepare(int ref, unsigned node, unsigned player, unsigned xp, unsigned claimed, unsigned ccr) {
    unsigned stride=ref?0x16:0x14, current=XP_NODES+node*stride, actor=XP_ACTOR+player*0x84;
    memset(g_ram+XP_ACTOR,0,0x2000);
    memset(g_lair_xp_awarded,0,sizeof(g_lair_xp_awarded));
    for (unsigned i=0;i<24;i++) {
        unsigned a=XP_NODES+i*stride;
        w32(a,XP_ITEMS+i*24); w32(a+4,0x0024000e); w16(a+8,23);
        w32(a+10,0x005000b8); w16(a+14,4); w32(a+16,0x0003123a);
    }
    w32(ref?0x2ddb2:0x2dfda,XP_NODES);
    w32(ref?0x36f28:0x37178,current); w32(ref?0x2e9ac:0x2ebd0,actor);
    w16(actor+0x4e,xp); w32(actor+0x36,player); w8(actor+0x4d,0x0c);
    if (ref) w16(current+20,claimed); else g_lair_xp_awarded[node]=(uint8_t)claimed;
    w32(XP_STACK,XP_STOP);w16(XP_STOP,0x60fe);
    m68k_set_reg(M68K_REG_SR,0x2700|ccr);
    m68k_set_reg(M68K_REG_A7,XP_STACK);
    m68k_set_reg(M68K_REG_A1,0x123456);
    m68k_set_reg(M68K_REG_PC,ref?0x21d10:0x21cf4);
    g_os=1;g_retail_parity=!ref;g_lair_xp_fix=1;g_lineage=ref?LIN_RETAIL:LIN_CRACKED;
    return current;
}

static void lair_checks(const char *dir, const char *retail) {
    char path[1024];
    snprintf(path,sizeof(path),"%s/current.sav",dir);lair_load(path);
    uint8_t *current=malloc(RAM_SIZE), *reference=malloc(RAM_SIZE);
    assert(current && reference);memcpy(current,g_ram,RAM_SIZE);
    lair_load(retail);memcpy(reference,g_ram,RAM_SIZE);
    const unsigned xp_values[]={0,1,2,0x7fff,0xfffe,0xffff};
    unsigned comparisons=0;
    for(unsigned node=0;node<24;node++) for(unsigned player=0;player<4;player++)
    for(unsigned visit=0;visit<2;visit++) {
        unsigned values[2][3];
        for(int ref=0;ref<2;ref++) {
            memcpy(g_ram,ref?reference:current,RAM_SIZE);
            unsigned a=lair_prepare(ref,node,player,9,visit,31), actor=XP_ACTOR+player*0x84;
            uint8_t before[24*0x16];memcpy(before,g_ram+XP_NODES,ref?24*0x16:24*0x14);
            lair_finish(ref?0x21d30:0x21d02);
            values[ref][0]=r16(actor+0x4e);values[ref][1]=m68k_get_reg(NULL,M68K_REG_SR);
            values[ref][2]=ref?r16(a+20):g_lair_xp_awarded[node];
            assert(m68k_get_reg(NULL,M68K_REG_A1)==(visit?a:actor));
            if(ref) before[node*0x16+21]=1;
            assert(!memcmp(before,g_ram+XP_NODES,ref?24*0x16:24*0x14));
        }
        assert(!memcmp(values[0],values[1],sizeof(values[0])));
        assert(values[0][0]==(visit?9:10));comparisons++;
    }
    for(unsigned ccr=0;ccr<32;ccr++) for(unsigned x=0;x<6;x++) for(unsigned visit=0;visit<2;visit++) {
        unsigned values[2][2];
        for(int ref=0;ref<2;ref++) {
            memcpy(g_ram,ref?reference:current,RAM_SIZE);lair_prepare(ref,23,3,xp_values[x],visit,ccr);
            if (!ref && visit) lair_original_hook(0x21cf4);
            else lair_finish(ref?0x21d2e:0x21d00);
            assert(m68k_get_reg(NULL,M68K_REG_PC)==(ref?0x21d2e:0x21d00));
            values[ref][0]=r16(XP_ACTOR+3*0x84+0x4e);values[ref][1]=m68k_get_reg(NULL,M68K_REG_SR);
        }
        assert(!memcmp(values[0],values[1],sizeof(values[0])));comparisons++;
    }
    printf("PASS: %u retail reward comparisons, all24 lairs/four players, first/repeat, XP overflow and all CCR states\n",comparisons);

    memcpy(g_ram,current,RAM_SIZE);lair_prepare(0,7,0,0,0,0);
    for(unsigned player=0;player<4;player++) {
        unsigned actor=XP_ACTOR+player*0x84;
        w32(0x2ebd0,actor);m68k_set_reg(M68K_REG_PC,0x21cf4);lair_finish(0x21d02);
        assert(r16(actor+0x4e)==(player==0));
    }
    /* Every actual instruction boundary survives a cold/warm save. */
    const unsigned boundaries[]={0x21cf4,0x21cfa,0x21d00};
    for(unsigned i=0;i<3;i++) {
        memcpy(g_ram,current,RAM_SIZE);lair_prepare(0,7,0,0,0,0);
        lair_finish(boundaries[i]);
        snprintf(path,sizeof(path),"%s/boundary-%x.sav",dir,boundaries[i]);assert(save_state(path));
        memset(g_lair_xp_awarded,1,24);w16(XP_ACTOR+0x4e,90);lair_load(path);
        lair_finish(0x21d02);assert(r16(XP_ACTOR+0x4e)==1 && g_lair_xp_awarded[7]);
        m68k_set_reg(M68K_REG_PC,0x21cf4);lair_finish(0x21d02);assert(r16(XP_ACTOR+0x4e)==1);
    }
    puts("PASS: one shared reward across players and save/reload at every award boundary");

    for(int ref=0;ref<2;ref++) for(unsigned item=0;item<26;item++) {
        memcpy(g_ram,ref?reference:current,RAM_SIZE);
        unsigned node=lair_prepare(ref,3,0,1,1,0), items=r32(node);
        w16(node+8,item==24);if(item<24)w8(items+item,1);
        w16(ref?0x2f7ae:0x2f9f0,0);
        m68k_set_reg(M68K_REG_PC,ref?0x21d50:0x21d22);lair_finish(XP_STOP);
        assert(r32(node+10)==(item==25?0xffffffffu:0x005000b8u));
        assert((ref?r16(node+20):g_lair_xp_awarded[3])==1);
    }
    puts("PASS: all24 loot slots and gold retain the lair; empty lairs close in both original routines");

    for(int mode=0;mode<3;mode++) {
        memcpy(g_ram,current,RAM_SIZE);lair_prepare(0,0,0,0,0,0);
        memset(g_lair_xp_awarded,1,24);g_retail_parity=mode!=1;g_lair_xp_fix=mode!=2;
        /* Actual initializer loop, including every node/inventory pointer. */
        w32(0x2dfde,XP_ITEMS);m68k_set_reg(M68K_REG_PC,0x25dfe);lair_finish(0x25e42);
        for(unsigned i=0;i<24;i++) assert(g_lair_xp_awarded[i]==0);
    }
    puts("PASS: new-world initialization resets every reward, including both disabled modes");

    /* Defeats bypass the reward block altogether. */
    for(int ref=0;ref<2;ref++) {
        memcpy(g_ram,ref?reference:current,RAM_SIZE);lair_prepare(ref,0,0,5,0,0);
        w8(ref?0x2de3e:0x2e066,1);
        m68k_set_reg(M68K_REG_PC,ref?0x21cf4:0x21cd8);lair_finish(ref?0x21cfe:0x21ce2);
        assert(r16(XP_ACTOR+0x4e)==5 && (ref?r16(XP_NODES+20):g_lair_xp_awarded[0])==0);
    }
    puts("PASS: original defeat branch gives no XP and does not consume the reward");

    for(unsigned test=0;test<16;test++) {
        memcpy(g_ram,current,RAM_SIZE);lair_prepare(0,3,0,5,1,0x1f);
        if(test==0)g_os=0; if(test==1)g_retail_parity=0;if(test==2)g_lair_xp_fix=0;
        if(test==3)g_lineage=LIN_RETAIL;if(test==4)g_lineage=LIN_UNKNOWN;
        if(test==5)w32(0x2dfda,0);if(test==6)w32(0x2dfda,XP_NODES+1);
        if(test==7)w32(0x2dfda,RAM_SIZE-2);if(test==8)w32(0x37178,XP_NODES-20);
        if(test==9)w32(0x37178,XP_NODES+24*20);if(test==10)w32(0x37178,XP_NODES+1);
        if(test==11)w32(0x2ebd0,0);if(test==12)w32(0x2ebd0,RAM_SIZE-2);
        if(test==13)w16(0x21cf4,0x4e71);if(test==14)w16(0x21cfe,0x0050);
        if(test==15)w16(0x21d00,0x7003);
        lair_original_hook(0x21cf4);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==0x21cf4);
        assert(m68k_get_reg(NULL,M68K_REG_SR)==0x271f && r16(XP_ACTOR+0x4e)==5);
    }
    puts("PASS: 16 mode/lineage/code/pointer scope guards");

    snprintf(path,sizeof(path),"%s/history.sav",dir);lair_load(path);
    for(unsigned i=0;i<24;i++)assert(g_lair_xp_awarded[i]==i%2);
    snprintf(path,sizeof(path),"%s/roundtrip.sav",dir);assert(save_state(path));
    memset(g_lair_xp_awarded,0,24);lair_load(path);
    for(unsigned i=0;i<24;i++)assert(g_lair_xp_awarded[i]==i%2);
    for(unsigned ver=2;ver<=4;ver++) {
        memset(g_lair_xp_awarded,1,24);snprintf(path,sizeof(path),"%s/legacy-%u.sav",dir,ver);lair_load(path);
        for(unsigned i=0;i<24;i++)assert(g_lair_xp_awarded[i]==0);
    }
    const char *bad[]={"invalid-first.sav","invalid-last.sav","invalid-sword.sav","invalid-ko.sav",
                      "truncated.sav","trailing.sav","invalid-streamer.sav","absent.sav"};
    snprintf(path,sizeof(path),"%s/history.sav",dir);lair_load(path);
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        size_t nb,na;uint8_t *b=lair_machine(&nb),*stream=g_loadbuf;
        snprintf(path,sizeof(path),"%s/%s",dir,bad[i]);assert(!load_state(path));
        uint8_t *a=lair_machine(&na);assert(nb==na && !memcmp(b,a,nb) && stream==g_loadbuf);free(a);free(b);
    }
    puts("PASS: all reward bits roundtrip; v2/v3/v4 loads reset missing history; eight failed loads are atomic");
    free(current);free(reference);
}

int main(int argc,char **argv) {
    const char *dir=NULL,*retail=NULL;int n=1,have_log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--lair-trace"))lair_trace=1;
        else if(!strcmp(argv[i],"--lair-tests") && i+2<argc) {dir=argv[++i];retail=argv[++i];}
        else if(!strcmp(argv[i],"--lair-snapshot") && i+2<argc) {
            lair_snapshot_pc=(unsigned)strtoul(argv[++i],NULL,16);lair_snapshot_path=argv[++i];
        } else if(!strcmp(argv[i],"--lair-stop") && i+1<argc)lair_stop_pc=(unsigned)strtoul(argv[++i],NULL,16);
        else {if(!strcmp(argv[i],"--log") && i+1<argc)have_log=1;argv[n++]=argv[i];}
    }
    if(!have_log){fprintf(stderr,"An explicit scratch --log is required.\n");return 2;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);if(rc)return rc;
    if(dir) {g_log=NULL;lair_trace=0;lair_checks(dir,retail);}
    return 0;
}
