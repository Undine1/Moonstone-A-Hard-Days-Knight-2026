/* Test-only Stonehenge oracle and native save-boundary capture. Never deploy.
 * Original modules are local, uncommitted inputs. Presentation is stubbed only
 * in --danu-oracle; ordinary capture runs the entire game and production hook. */
#define SDL_MAIN_HANDLED
#define moon_instr_hook danu_production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

#define TEST_ACTORS 0x100000u
#define TEST_ITEMS 0x100400u
#define TEST_STOP 0x1ef000u
static unsigned char test_baseline[RAM_SIZE];
static const char *capture_dir;
static const char *warm_load_path;
static unsigned capture_count, native_clicks, sound_pc;
static int oracle_mode, current_edition, capture_started, capture_finished, warm_loaded;

void moon_instr_hook(unsigned pc) {
    if (warm_load_path && !warm_loaded && pc == 0x22816 && r32(0x2fb1c) == 3) {
        warm_loaded = 1;
        assert(load_state(warm_load_path));
        if (g_log) fprintf(g_log,"DANU-WARM-LOADED %s\n",warm_load_path);
        return; /* the restored instruction, not the former caller's boundary */
    }
    if (pc == sound_pc) native_clicks++;
    if (capture_dir && !capture_finished) {
        if (pc == 0x2cda6u && r32(0x2fb1c) == 3) capture_started = 1;
        /* Every transaction instruction, plus native sound entry/return and
         * caller/reward boundaries. Do not save thousands of raster waits. */
        if (capture_started && ((pc >= 0x2cda6 && pc <= 0x2ce20)
            || (pc >= 0x2cfde && pc <= 0x2d014)
            || (pc >= 0x2cd6a && pc <= 0x2cd7c)
            || pc == 0x3aa44 || pc == 0x3aa4a
            || (pc >= 0x227de && pc <= 0x22816))) {
            char path[1200]; assert(capture_count < 100);
            snprintf(path, sizeof(path), "%s/mid-%02u.sav", capture_dir, capture_count++);
            assert(save_state(path));
            if (g_log) fprintf(g_log, "DANU-BOUNDARY index=%u pc=%x\n", capture_count-1, pc);
            if (pc == 0x22816) capture_finished = 1;
        }
    }
    if (!oracle_mode || current_edition) danu_production_hook(pc);
}

static void native_call(unsigned pc, unsigned a0, unsigned stop) {
    m68k_set_reg(M68K_REG_SR, 0x2700);
    for (int i=0; i<15; i++) m68k_set_reg((m68k_register_t)(M68K_REG_D0+i), 0);
    m68k_set_reg(M68K_REG_A0, a0);
    m68k_set_reg(M68K_REG_A7, 0x1ff000); w32(0x1ff000, TEST_STOP);
    m68k_set_reg(M68K_REG_PC, pc);
    unsigned steps=0;
    while (m68k_get_reg(NULL, M68K_REG_PC) != stop) {
        if (++steps > 200000) {
            fprintf(stderr,"timeout entry=%x pc=%x stop=%x\n",pc,
                    m68k_get_reg(NULL,M68K_REG_PC),stop); abort();
        }
        m68k_execute(1); assert(!g_stop);
    }
}

static void prepare(const char *dir, int edition, int sword, int xp,
                    int lives, int player, int price, int cached) {
    int retail = edition == 1;
    char path[1200]; snprintf(path,sizeof(path),"%s/%s.sav",dir,retail?"retail":"port");
    FILE *f=fopen(path,"rb"); assert(f); assert(!fseek(f,20,SEEK_SET));
    assert(fread(g_ram,1,RAM_SIZE,f)==RAM_SIZE); fclose(f);
    current_edition = edition == 2;
    g_os=current_edition; g_lineage=LIN_CRACKED; g_retail_parity=1; g_danu_fix=!cached;
    g_stop=0; g_icount=0;
    const unsigned drawing_port[]={0x2a17c,0x3f008,0x3ee32};
    const unsigned drawing_retail[]={0x2a038,0x3ecec,0x3eb16};
    for(unsigned i=0;i<3;i++) w16(retail?drawing_retail[i]:drawing_port[i],0x4e75);
    unsigned actor=TEST_ACTORS+player*0x84, items=TEST_ITEMS+player*0x18;
    for(unsigned n=0;n<4;n++) {
        unsigned a=TEST_ACTORS+n*0x84, inv=TEST_ITEMS+n*0x18;
        w32(a+0x60,inv); w32(a+0x58,sword?0x19:0x16); w32(a+0x5c,0x1b);
        w8(a+0x46,2); w8(a+0x47,2); w8(a+0x48,2); w8(a+0x49,lives);
        w16(a+0x4a,37); w8(a+0x4c,10); w16(a+0x4e,xp);
        w16(a+0x50,13); w8(a+0x82,1);
        for(unsigned i=0;i<0x14;i+=2) w8(inv+i,1);
        w8(inv+4,sword); w8(inv+0x14,1); w8(inv+0x16,1);
    }
    w32(retail?0x2de94:0x2e0bc,actor);
    w32(retail?0x2f8ca:0x2fb1c,3); w16(retail?0x302dc:0x30528,price);
    native_call(retail?0x2d4ba:0x2d6e0,0,TEST_STOP);
    w32(retail?0x2e9a8:0x2ebcc,actor);
    w32(retail?0x2f8ae:0x2fafe,retail?0x2fbf6:0x2fe44);
    w32(retail?0x390a8:0x392ea,0x181000);
    for(unsigned i=0;i<64;i++) { w16(0x18100e + i*10,8); w16(0x181010 + i*10,8); }
    unsigned regsite=retail?0x2a356:0x2a49a;
    assert(r16(regsite)==0x2079); w32(r32(regsite+2),0x184000);
    native_call(retail?0x2bf44:0x2c044,0,TEST_STOP);
    g_danu_fix=1;
    /* Preserve each original MOVEM helper, including its real registers/stack.
     * Only the audio backend is stubbed. Current uses the runtime relocation
     * expected by the production B15 helper guard (flat module hunks differ). */
    unsigned helper=retail?0x2cc36:0x2cd6a;
    sound_pc=current_edition?0x3aa44:r32(helper+10);
    if(current_edition) w32(helper+10,sound_pc);
    w16(sound_pc,0x4e75);
    w16(retail?0x2bb9a:0x2bca4,0x4e75);
    w16(retail?0x2b802:0x2b92a,0x4e75);
    memcpy(test_baseline,g_ram,RAM_SIZE);
    assert(r32(actor+0x60)==items);
}

static void original_matrix(const char *dir) {
    unsigned total=0;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); oracle_mode=1;
    for(int edition=0;edition<3;edition++) for(int player=0;player<4;player++)
    for(int sword=0;sword<2;sword++) for(int xp_case=0;xp_case<2;xp_case++)
    for(int lives=3;lives<=5;lives+=2) for(int cached=0;cached<=(edition==2);cached++) {
        int retail=edition==1,price=player==0?3:player==1?2:1,xp=xp_case?price:0;
        prepare(dir,edition,sword,xp,lives,player,price,cached);
        unsigned actor=TEST_ACTORS+player*0x84,items=TEST_ITEMS+player*0x18,seen[64]={0};
        for(unsigned n=0;n<98;n++) {
            memcpy(g_ram,test_baseline,RAM_SIZE); unsigned h=0x184000+n*24;
            if(!r16(h+4))break;
            unsigned action=r32(h+16); assert(action<108);
            if(seen[action/4]++)continue;
            unsigned arg=r16(h+22),kind=r16(h+20),flags=r16(r32(h+8)+8);
            unsigned selected=retail?0x2cea4:0x2cfda,exitflag=retail?0x390a4:0x392e6;
            w16(selected,0xffff); w16(exitflag,0); native_clicks=0;
            native_call(retail?0x2cc4a:0x2cd7e,h,TEST_STOP);
            unsigned result=r16(selected),exited=r16(exitflag);
            native_call(retail?0x22780:0x227de,0,retail?0x227b8:0x22816);
            fprintf(g_log,"{\"edition\":%d,\"player\":%d,\"cached\":%d,\"sword\":%d,\"xp\":%d,\"price\":%d,\"lives_before\":%d,\"arg\":%u,\"action\":%u,\"kind\":%u,\"flags\":%u,\"selected\":%u,\"exit\":%u,\"clicks\":%u,\"stats\":[%u,%u,%u],\"xp_after\":%u,\"lives\":%u,\"hp\":%u,\"max_hp\":%u,\"curse\":%u,\"weapon\":%u,\"counter\":%u,\"writes\":[",
                edition,player,cached,sword,xp,price,lives,arg,action,kind,flags,result,exited,native_clicks,
                r8(actor+0x46),r8(actor+0x47),r8(actor+0x48),r16(actor+0x4e),
                r8(actor+0x49),r16(actor+0x50),r16(actor+0x54),r8(actor+0x82),r32(actor+0x58),r16(retail?0x2f8b2:0x2fb02));
            unsigned count=0;
            for(unsigned i=0;i<0xc0;i++) if(g_ram[TEST_ITEMS+i]!=test_baseline[TEST_ITEMS+i])
                fprintf(g_log,"%s[%d,%u,%u]",count++?",":"",(int)(TEST_ITEMS+i-items),test_baseline[TEST_ITEMS+i],g_ram[TEST_ITEMS+i]);
            fputs("]}\n",g_log); total++;
        }
    }
    printf("Completed %u original/current native builder/click/reward cases\n",total);
}

static void guard_matrix(const char *dir) {
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); oracle_mode=1;
    prepare(dir,2,0,3,3,0,3,0);
    unsigned char *before=malloc(RAM_SIZE); assert(before);
    const unsigned sites[]={0x2c86a,0x2cda6,0x2ce18,0x2cfde,0x2cfee,
                            0x2cff4,0x2cffc,0x2d010,0x2d014,0x227de};
    const unsigned signature[]={0x2c86a,0x2c86e,0x2cfde,0x2cfe2,0x2cfe6,0x2cffc,0x2d010};
    unsigned total=0;
    for(unsigned s=0;s<sizeof(sites)/sizeof(sites[0]);s++)for(unsigned mode=0;mode<24;mode++) {
        memcpy(g_ram,test_baseline,RAM_SIZE);
        g_os=g_retail_parity=g_danu_fix=1; g_lineage=LIN_CRACKED;
        unsigned pc=sites[s];
        m68k_set_reg(M68K_REG_PC,pc);m68k_set_reg(M68K_REG_D0,0x10);m68k_set_reg(M68K_REG_D1,0);
        m68k_set_reg(M68K_REG_A1,TEST_ITEMS);
        if(mode==0)g_os=0; if(mode==1)g_retail_parity=0; if(mode==2)g_danu_fix=0;
        if(mode==3)g_lineage=LIN_RETAIL; if(mode==4)g_lineage=LIN_UNKNOWN;
        if(mode>=5 && mode<17) {unsigned scene=mode-5;w32(0x2fb1c,scene>=3?scene+1:scene);}
        if(mode>=17)w8(signature[mode-17],r8(signature[mode-17])^1);
        memcpy(before,g_ram,RAM_SIZE);
        unsigned regs[18];for(unsigned r=0;r<18;r++)regs[r]=m68k_get_reg(NULL,(m68k_register_t)r);
        danu_hook(pc);
        assert(!memcmp(before,g_ram,RAM_SIZE));
        for(unsigned r=0;r<18;r++)assert(regs[r]==m68k_get_reg(NULL,(m68k_register_t)r));
        total++;
    }
    prepare(dir,2,0,3,3,0,3,0);
    /* A stale cached click with no item, or a second click after consumption,
     * must be inert even if its label still says Offer. */
    for(unsigned arg=0;arg<20;arg+=2) {
        if(arg==4)continue;
        memcpy(g_ram,test_baseline,RAM_SIZE);
        unsigned h=0;
        for(unsigned n=0;n<98;n++)if(r16(0x184000+n*24+22)==arg){h=0x184000+n*24;break;}
        assert(h);
        for(unsigned n=0;n<2;n++) {
            w16(0x2cfda,0xffff);w16(0x392e6,0);native_clicks=0;
            native_call(0x2cd7e,h,TEST_STOP);
            assert(r8(TEST_ITEMS+arg)==0 && native_clicks==(n==0));
            assert(r16(0x2cfda)==(n==0?arg:0xffff));
            assert(r16(0x392e6)==(n==0));
        }
    }
    free(before);
    printf("PASS: %u disabled/scene/lineage/layout guards; nine repeated stale-item clicks cannot underflow or repeat\n",total);
}

int main(int argc,char **argv) {
    const char *oracle=NULL,*guards=NULL,*log=NULL; int n=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--danu-oracle") && i+1<argc) oracle=argv[++i];
        else if(!strcmp(argv[i],"--danu-guards") && i+1<argc) guards=argv[++i];
        else if(!strcmp(argv[i],"--danu-capture") && i+1<argc) capture_dir=argv[++i];
        else if(!strcmp(argv[i],"--danu-warm-load") && i+1<argc) warm_load_path=argv[++i];
        else { if(!strcmp(argv[i],"--log") && i+1<argc) log=argv[i+1]; argv[n++]=argv[i]; }
    }
    assert(log); argv[n]=NULL;
    if(oracle) {g_log=fopen(log,"w");assert(g_log);original_matrix(oracle);fclose(g_log);return 0;}
    if(guards) {g_log=fopen(log,"w");assert(g_log);guard_matrix(guards);fclose(g_log);return 0;}
    int rc=moonstone_main(n,argv);
    if(!rc && capture_dir)assert(capture_finished && capture_count);
    if(!rc && warm_load_path)assert(warm_loaded);
    return rc;
}
