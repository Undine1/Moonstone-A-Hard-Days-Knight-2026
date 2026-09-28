/* Test-only original sound execution and save-boundary capture. Never deploy.
 * Every invocation requires an explicit scratch --log. */
#define SDL_MAIN_HANDLED
#define moon_instr_hook sfx_production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static const char *sfx_capture_dir;
static unsigned sfx_capture_site, sfx_capture_count, sfx_requests;
static int sfx_capturing, sfx_captured;
void moon_instr_hook(unsigned pc) {
    unsigned sp=m68k_get_reg(NULL,M68K_REG_A7);
    if(pc==0x3aa4au && (m68k_get_reg(NULL,M68K_REG_D0)&65535)==0x9c) {
        sfx_requests++;
        if(g_log)fprintf(g_log,"SFX-REQUEST fr=%d return=%x\n",g_cur_frame,r32(sp));
    }
    if(sfx_capture_dir && !sfx_captured && pc==0x2cd6eu && sp<RAM_SIZE-76u
       && r32(sp+64u)==SFX_RETURN_TAG) {
        sfx_capture_site=r32(sp+68u);sfx_capturing=1;sfx_captured=1;
    }
    if(sfx_capturing) {
        char path[1200];
        assert(sfx_capture_count<128);
        snprintf(path,sizeof(path),"%s/mid-%02u.sav",sfx_capture_dir,sfx_capture_count++);
        assert(save_state(path));
        if(g_log)fprintf(g_log,"SFX-BOUNDARY index=%u pc=%x site=%x\n",sfx_capture_count-1,pc,sfx_capture_site);
        if(pc==sfx_capture_site)sfx_capturing=0;
    }
    sfx_production_hook(pc);
}

static void sfx_until(unsigned pc) {
    unsigned steps=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=pc) {
        assert(++steps<512);m68k_execute(1);assert(!g_stop);
    }
}
static void sfx_seed(unsigned pc,unsigned flags) {
    g_os=1;g_lineage=LIN_CRACKED;g_retail_parity=1;g_retail_sfx=1;g_stop=0;
    m68k_set_reg(M68K_REG_SR,0x2700|flags);
    for(unsigned i=0;i<15;i++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+i),0x100000u+4u*i);
    m68k_set_reg(M68K_REG_A7,0x1ff000);m68k_set_reg(M68K_REG_PC,pc);sfx_requests=0;
}
static void sfx_assert_registers(unsigned pc,unsigned flags) {
    for(unsigned i=0;i<15;i++)assert(m68k_get_reg(NULL,(m68k_register_t)(M68K_REG_D0+i))==0x100000u+4u*i);
    assert(m68k_get_reg(NULL,M68K_REG_A7)==0x1ff000);
    assert(m68k_get_reg(NULL,M68K_REG_SR)==(0x2700|flags));
    assert(m68k_get_reg(NULL,M68K_REG_PC)==pc);
}
static void sfx_matrix(const char *dir) {
    uint8_t *original=malloc(RAM_SIZE);assert(original);memcpy(original,g_ram,RAM_SIZE);
    assert(retail_sfx_helper());
    for(unsigned i=0;i<sizeof(g_sfx_sites)/sizeof(g_sfx_sites[0]);i++)for(unsigned flags=0;flags<32;flags++) {
        memcpy(g_ram,original,RAM_SIZE);unsigned pc=g_sfx_sites[i].pc;sfx_seed(pc,flags);
        assert(retail_sfx_site(pc));retail_sfx_begin(pc);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2cd6a);
        sfx_until(pc);assert(sfx_requests==1);assert(retail_sfx_resume(pc));
        sfx_assert_registers(pc,flags);assert(!retail_sfx_resume(pc));
    }
    puts("PASS: all25 click sites x32 CCR combinations preserve every register, SR and stack; exactly one native request");
    const unsigned site=0x2d428;
    for(unsigned mode=0;mode<12;mode++) {
        memcpy(g_ram,original,RAM_SIZE);sfx_seed(site,31);
        if(mode==0)g_os=0;if(mode==1)g_retail_parity=0;if(mode==2)g_retail_sfx=0;
        if(mode==3)g_lineage=LIN_RETAIL;if(mode==4)g_lineage=LIN_UNKNOWN;
        if(mode==5)w16(site,0x4e71);if(mode==6)w16(0x2cd6a,0x4e71);
        if(mode==7)m68k_set_reg(M68K_REG_A7,0x1ff001);
        if(mode==8)m68k_set_reg(M68K_REG_A7,74);
        if(mode==9)m68k_set_reg(M68K_REG_A7,RAM_SIZE+2);
        if(mode==10)w32(0x22386,0x2c5f4);
        if(mode==11)w32(0x22cd6,0x2e05a);
        retail_sfx_begin(mode==10?0x22384:mode==11?0x22cda:site);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==site && !sfx_requests);
    }
    puts("PASS: disabled modes, lineages, altered opcode/operand/helper/name block and invalid stack remain inert");
    /* Save every instruction boundary, then warm-load after a separate completed
     * call. No host-only phase should survive or be needed by the restored call. */
    memcpy(g_ram,original,RAM_SIZE);sfx_seed(site,31);retail_sfx_begin(site);
    unsigned count=0;char path[1200];
    do {
        snprintf(path,sizeof(path),"%s/unit-mid-%02u.sav",dir,count++);assert(save_state(path));
        if(m68k_get_reg(NULL,M68K_REG_PC)==site)break;
        assert(count<128);m68k_execute(1);assert(!g_stop);
    } while(1);
    for(unsigned mode=0;mode<3;mode++)for(unsigned n=0;n<count;n++) {
        memcpy(g_ram,original,RAM_SIZE);sfx_seed(site,0);retail_sfx_begin(site);sfx_until(site);
        assert(retail_sfx_resume(site));
        snprintf(path,sizeof(path),"%s/unit-mid-%02u.sav",dir,n);assert(load_state(path));
        g_retail_parity=mode!=1;g_retail_sfx=mode!=2;
        sfx_until(site);assert(retail_sfx_resume(site));sfx_assert_registers(site,31);
        assert(!retail_sfx_resume(site));
    }
    printf("PASS: %u native call boundaries reload after another click; pending calls finish with parity or added sounds disabled\n",count);
    memcpy(g_ram,original,RAM_SIZE);sfx_seed(site,0);
    retail_sfx_begin(site); /* replace one pending call with another saved call */
    snprintf(path,sizeof(path),"%s/unit-mid-%02u.sav",dir,count-1);assert(load_state(path));
    assert(retail_sfx_resume(site));sfx_assert_registers(site,31);
    memcpy(g_ram,original,RAM_SIZE);sfx_seed(site,7);
    snprintf(path,sizeof(path),"%s/unit-ordinary.sav",dir);assert(save_state(path));
    retail_sfx_begin(site);sfx_until(0x2cd6eu);
    assert(load_state(path));assert(!retail_sfx_resume(site));sfx_assert_registers(site,7);
    retail_sfx_begin(site);sfx_until(site);assert(retail_sfx_resume(site));
    sfx_assert_registers(site,7);
    puts("PASS: loading an ordinary save during sound discards the pending call without stale host state");
    free(original);
}

int main(int argc,char **argv) {
    const char *tests=NULL;int n=1,have_log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--sfx-capture") && i+1<argc)sfx_capture_dir=argv[++i];
        else if(!strcmp(argv[i],"--sfx-tests") && i+1<argc)tests=argv[++i];
        else {if(!strcmp(argv[i],"--log") && i+1<argc)have_log=1;argv[n++]=argv[i];}
    }
    if(!have_log){fprintf(stderr,"Explicit scratch --log required.\n");return 2;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);if(rc)return rc;
    g_log=NULL;
    if(sfx_capture_dir)assert(sfx_captured && !sfx_capturing);
    if(tests)sfx_matrix(tests);
    return 0;
}
