/* Test-only observations of the original AI-knight routines. Never deploy.
 * Every run needs an explicit scratch --log. */
#define moon_instr_hook knight_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int knight_trace, knight_count;
static unsigned knight_snapshot_pc, knight_stop_pc;
static const char *knight_snapshot_path;
static int knight_snapshot_flag;

static void knight_finish(unsigned stop) {
    unsigned steps=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=stop && steps++<10000) m68k_execute(1);
    if(m68k_get_reg(NULL,M68K_REG_PC)!=stop)
        fprintf(stderr,"knight_finish pc=%06x wanted=%06x steps=%u enabled=%d\n",
                m68k_get_reg(NULL,M68K_REG_PC),stop,steps,g_knight_reaction_fix);
    assert(m68k_get_reg(NULL,M68K_REG_PC)==stop);
}

static void knight_load(const char *dir,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *log=g_log;g_log=NULL;assert(load_state(path));g_log=log;
}

static void knight_checks(const char *dir,const char *retail) {
    const unsigned actor=0x100000;
    /* The added BCLR must preserve every other flag bit and every other CCR
     * bit. Compare directly to the unmodified retail 68000 instruction. */
    for(unsigned ccr=0;ccr<32;ccr++) for(unsigned flags=0;flags<256;flags+=0x25) {
        unsigned values[2][2];
        for(unsigned ref=0;ref<2;ref++) {
            if(ref) { FILE *log=g_log;g_log=NULL;assert(load_state(retail));g_log=log;g_retail_parity=0; }
            else { knight_load(dir,"block-entry.sav");g_retail_parity=1; }
            g_knight_reaction_fix=1;g_lineage=LIN_CRACKED;
            w8(actor+0x4d,0x10);w16(actor+0x68,(flags<<8)|0x5a);
            m68k_set_reg(M68K_REG_A1,actor);m68k_set_reg(M68K_REG_SR,0x2700|ccr);
            if(ref) {m68k_set_reg(M68K_REG_PC,0x42428);m68k_execute(1);}
            else knight_original_hook(0x427c4);
            values[ref][0]=r16(actor+0x68);values[ref][1]=m68k_get_reg(NULL,M68K_REG_SR);
        }
        assert(!memcmp(values[0],values[1],sizeof(values[0])));
    }
    puts("PASS: native retail BCLR comparison for 32 CCR states and seven flag patterns");

    for(int enabled=0;enabled<2;enabled++) for(int repeat=0;repeat<3;repeat++) {
        knight_load(dir,"reaction-entry.sav");g_retail_parity=1;g_knight_reaction_fix=enabled;
        knight_finish(0x27ec8);
        assert(r16(actor+0x40)==(enabled?0x10:8));
        knight_load(dir,"block-entry.sav");knight_finish(0x27ec8);
        assert(r16(actor+0x68)==(enabled?0x005a:0x805a));
    }
    /* A saved PC can fall between the old BTST and BNE. Treat both instructions
     * as deleted, even if the saved Z would have taken the obsolete branch. */
    for(int z=0;z<2;z++) {
        knight_load(dir,"reaction-entry.sav");g_knight_reaction_fix=1;
        m68k_set_reg(M68K_REG_PC,0x42622);m68k_set_reg(M68K_REG_SR,0x2700|(z?4:0));
        char path[1024];snprintf(path,sizeof(path),"%s/branch-roundtrip.sav",dir);
        assert(save_state(path));w8(actor+0x68,0);assert(load_state(path));
        knight_finish(0x27ec8);assert(r16(actor+0x40)==0x10);
    }
    puts("PASS: repeated warm loads, isolated rollback and old mid-gate save roundtrips");

    /* Repeated matching contacts must retain retail blocking and health. The
     * shared helper itself remains unchanged, including its human latch rule. */
    for(int enabled=0;enabled<2;enabled++) {
        knight_load(dir,"block-fixed.sav");g_knight_reaction_fix=enabled;g_retail_parity=1;
        knight_finish(0x27ec8);assert(r16(0x2647a)==1);
        for(int i=0;i<4;i++) {
            m68k_set_reg(M68K_REG_PC,0x4278c);knight_finish(0x27ec8);
            assert(r16(0x2647a)==(enabled?1:0));
            assert((r16(actor+0x50)==100)==enabled);
        }
    }
    puts("PASS: five consecutive actual contacts; retail recovery versus inherited one-block latch");

    for(int site=0;site<2;site++) for(int test=0;test<14;test++) {
        unsigned pc=site?0x427c4:0x4261c;
        knight_load(dir,site?"block-entry.sav":"reaction-entry.sav");
        g_os=1;g_retail_parity=1;g_knight_reaction_fix=1;g_lineage=LIN_CRACKED;
        if(test==0) g_knight_reaction_fix=0;
        if(test==1) g_retail_parity=0;
        if(test==2) g_os=0;
        if(test==3) g_lineage=LIN_UNKNOWN;
        if(test==4) g_lineage=LIN_RETAIL;
        if(test>=5 && test<=7) w8(actor+0x4d,test==5?0x0c:test==6?0x24:8);
        if(test==8) m68k_set_reg(site?M68K_REG_A1:M68K_REG_A0,0);
        if(test==9) m68k_set_reg(site?M68K_REG_A1:M68K_REG_A0,actor+1);
        if(test==10) m68k_set_reg(site?M68K_REG_A1:M68K_REG_A0,RAM_SIZE-4);
        if(test==11) w16(site?0x427bc:0x4261c,0x4e71);
        if(test==12) w16(site?0x427c2:0x42624,0x1234);
        if(test==13) w16(site?0x427c8:0x42626,0x4321);
        uint32_t sr=m68k_get_reg(NULL,M68K_REG_SR);
        knight_original_hook(pc);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==pc);
        assert(m68k_get_reg(NULL,M68K_REG_SR)==sr && r16(actor+0x68)==0x805a);
    }
    g_os=1;g_retail_parity=1;g_knight_reaction_fix=1;g_lineage=LIN_CRACKED;
    puts("PASS: 28 scope guards, including human/rat/demon actors, code signatures, lineage and disabled modes");
}

void moon_instr_hook(unsigned pc) {
    unsigned actor = r32(0x2ebd0), target = r32(0x2ebd4);
    if (knight_trace && knight_count < 12000
        && (pc == 0x4250e || pc == 0x4261c || pc == 0x42670 || pc == 0x426a0
            || pc == 0x427c4 || pc == 0x427ec || pc == 0x2645e || pc == 0x26474
            || pc == 0x262b6 || pc == 0x27ec8)) {
        if (pc == 0x4250e) actor = m68k_get_reg(NULL, M68K_REG_A0);
        if (pc == 0x2645e || pc == 0x26474 || pc == 0x262b6)
            actor = m68k_get_reg(NULL, M68K_REG_A1);
        if (actor && actor < RAM_SIZE - 0x84 && target < RAM_SIZE - 0x84) {
            fprintf(g_log,"KNIGHT-TEST pc=%06x fr=%d actor=%06x kind=%02x state=%04x action=%02x hp=%d x=%d y=%d target=%06x targetaction=%02x targethp=%d targetx=%d targety=%d rng=%08x anim=%06x block=%u\n",
                pc,g_cur_frame,actor,r8(actor+0x4d),r16(actor+0x68),r16(actor+0x40),
                (int16_t)r16(actor+0x50),(int16_t)r16(actor+4),(int16_t)r16(actor+8),
                target,r16(target+0x40),(int16_t)r16(target+0x50),
                (int16_t)r16(target+4),(int16_t)r16(target+8),r32(0x391a8),
                r32(0x2eaf8),r16(0x2647a));
            knight_count++;
        }
    }
    if (knight_snapshot_path && pc == knight_snapshot_pc
        && (!knight_snapshot_flag || (r8(actor+0x68)&0x80))) {
        assert(save_state(knight_snapshot_path));
        fprintf(g_log,"KNIGHT-SNAPSHOT pc=%06x fr=%d\n",pc,g_cur_frame);
        knight_snapshot_path=NULL;
    }
    if (knight_stop_pc && pc == knight_stop_pc) { halt("knight probe stop"); return; }
    knight_original_hook(pc);
}

int main(int argc,char **argv) {
    int n=1,have_log=0;
    const char *checks=NULL,*retail=NULL;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--knight-trace")) knight_trace=1;
        else if (!strcmp(argv[i],"--knight-check-dir") && i+1<argc) checks=argv[++i];
        else if (!strcmp(argv[i],"--knight-retail") && i+1<argc) retail=argv[++i];
        else if (!strcmp(argv[i],"--knight-flagged")) knight_snapshot_flag=1;
        else if (!strcmp(argv[i],"--knight-stop") && i+1<argc)
            knight_stop_pc=(unsigned)strtoul(argv[++i],NULL,16);
        else if (!strcmp(argv[i],"--knight-snapshot") && i+2<argc) {
            knight_snapshot_pc=(unsigned)strtoul(argv[++i],NULL,16);
            knight_snapshot_path=argv[++i];
        } else {
            if (!strcmp(argv[i],"--log") && i+1<argc) have_log=1;
            argv[n++]=argv[i];
        }
    }
    if (!have_log) { fputs("An explicit scratch --log is required.\n",stderr);return 2; }
    argv[n]=NULL;
    int rc=moonstone_main(n,argv);
    if(rc)return rc;
    if(checks) {
        assert(retail);knight_trace=0;knight_snapshot_path=NULL;knight_stop_pc=0;
        setvbuf(stdout,NULL,_IONBF,0);
        g_log=stdout;knight_checks(checks,retail);g_log=NULL;
    }
    return 0;
}
