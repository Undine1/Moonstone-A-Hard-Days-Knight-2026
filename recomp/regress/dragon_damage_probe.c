/* Test-only observations and actual warm save/load checks. Never deploy.
 * Every invocation must have an explicit scratch --log. */
#define moon_instr_hook dragon_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static unsigned dragon_snapshot_pc, dragon_stop_pc;
static const char *dragon_snapshot_path;
static int dragon_trace, dragon_trace_count;

void moon_instr_hook(unsigned pc) {
    if (dragon_trace && dragon_trace_count < 120
        && (pc == 0x26682 || pc == 0x2668c || pc == 0x2667e
            || pc == 0x2222a || pc == 0x2224e || pc == 0x27a08)) {
        unsigned a0=m68k_get_reg(NULL,M68K_REG_A0), a1=m68k_get_reg(NULL,M68K_REG_A1);
        fprintf(g_log,"DRAGON-TEST pc=%06x fr=%d a0=%06x a1=%06x d0=%08x hp=%d lives=%u kind=%02x foot2=%06x\n",
                pc,g_cur_frame,a0,a1,m68k_get_reg(NULL,M68K_REG_D0),
                (int16_t)r16(0x2e82c),r8(0x2e825),r8(a0+0x4d),r32(0x25638));
        dragon_trace_count++;
    }
    if (dragon_stop_pc && pc == dragon_stop_pc) { halt("dragon probe stop"); return; }
    dragon_original_hook(pc);
    if (dragon_snapshot_path && pc == dragon_snapshot_pc) {
        assert(save_state(dragon_snapshot_path));
        fprintf(g_log,"DRAGON-SNAPSHOT pc=%06x opcode=%04x\n",pc,r16(0x26682));
        dragon_snapshot_path=NULL;
    }
}

static void dragon_warm_checks(const char *dir) {
    char path[1024];
    const char *names[]={"entry-old.sav","entry-new.sav","entry-new.sav","entry-new.sav","entry-old.sav"};
    const int enabled[]={1,1,0,1,1}, parity[]={1,1,1,0,1};
    const int expected[]={90,90,66,66,90};
    for (unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);i++) {
        snprintf(path,sizeof(path),"%s/%s",dir,names[i]);
        assert(load_state(path));
        g_dragon_damage_fix=enabled[i];g_retail_parity=parity[i];
        m68k_execute(1000);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2668c);
        assert((int16_t)r16(0x100150)==expected[i]);
    }
    puts("PASS: warm old/new saves, isolated A/B, parity-off and repeated F9 restore the expected damage");

    g_dragon_damage_fix=g_retail_parity=1;
    snprintf(path,sizeof(path),"%s/entry-old.sav",dir);assert(load_state(path));
    m68k_execute(1);
    assert(m68k_get_reg(NULL,M68K_REG_PC)==0x26686 && r16(0x26682)==0x303c);
    snprintf(path,sizeof(path),"%s/roundtrip.sav",dir);assert(save_state(path));
    m68k_set_reg(M68K_REG_D0,0);w16(0x26682,0x0440);
    assert(load_state(path));m68k_execute(1000);
    assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2668c && r16(0x100150)==90);
    puts("PASS: real mid-instruction save/load roundtrip keeps the corrected calculation");

    /* Execute each actual MOVE for every incoming condition-code combination.
     * This checks X preservation and cycle count, not a host arithmetic copy. */
    for (unsigned ccr=0;ccr<32;ccr++) {
        uint32_t values[2][3];
        for (unsigned reference=0;reference<2;reference++) {
            snprintf(path,sizeof(path),"%s/%s",dir,reference?"entry-retail.sav":"entry-old.sav");
            assert(load_state(path));
            m68k_set_reg(M68K_REG_SR,0x2700|ccr);
            m68k_set_reg(M68K_REG_D0,0x1234002c);
            values[reference][0]=(unsigned)m68k_execute(1);
            values[reference][1]=m68k_get_reg(NULL,M68K_REG_D0);
            values[reference][2]=m68k_get_reg(NULL,M68K_REG_SR);
        }
        assert(!memcmp(values[0],values[1],sizeof(values[0])));
        assert(values[0][1]==0x1234000a);
    }
    puts("PASS: all 32 incoming CCR combinations preserve retail MOVE flags, upper D0 and cycles");

    for (int test=0;test<5;test++) {
        snprintf(path,sizeof(path),"%s/entry-old.sav",dir);assert(load_state(path));
        g_lineage=LIN_CRACKED;g_os=1;
        if (test==0) w16(0x26684,11);
        if (test==1) w16(0x26686,0x612a);
        if (test==2) w16(0x2668a,0x0052);
        if (test==3) g_lineage=LIN_UNKNOWN;
        if (test==4) g_lineage=LIN_RETAIL;
        dragon_original_hook(0x26682);
        assert(r16(0x26682)==0x0440);
    }
    puts("PASS: mismatched immediate, continuation and non-target lineages are untouched");
}

int main(int argc,char **argv) {
    int n=1,have_log=0;const char *warm_dir=NULL;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--dragon-trace")) dragon_trace=1;
        else if (!strcmp(argv[i],"--dragon-warm-dir") && i+1<argc) warm_dir=argv[++i];
        else if (!strcmp(argv[i],"--dragon-stop") && i+1<argc) dragon_stop_pc=(unsigned)strtoul(argv[++i],NULL,16);
        else if (!strcmp(argv[i],"--dragon-snapshot") && i+2<argc) {
            dragon_snapshot_pc=(unsigned)strtoul(argv[++i],NULL,16);dragon_snapshot_path=argv[++i];
        } else {
            if (!strcmp(argv[i],"--log") && i+1<argc) have_log=1;
            argv[n++]=argv[i];
        }
    }
    if (!have_log) {fputs("An explicit scratch --log is required.\n",stderr);return 2;}
    argv[n]=NULL;
    int rc=moonstone_main(n,argv);if(rc)return rc;
    if(warm_dir) {
        dragon_trace=0;dragon_stop_pc=0;dragon_snapshot_path=NULL;
        g_log=stdout;dragon_warm_checks(warm_dir);g_log=NULL;
    }
    return 0;
}
