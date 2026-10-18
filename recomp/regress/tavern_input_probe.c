/* Test-only tavern observer, native RNG/payout checks and input scope tests. */
#define SDL_MAIN_HANDLED
#include "../vendor/Musashi-master/m68k.h"
static int probe_execute(int cycles);
#define m68k_execute probe_execute
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef m68k_execute
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(86); } } while(0)

static const char *capture, *warm;
static int unfixed, warm_at=-1, restored, captured;
static int polls, bets, rolls, rng, acknowledgements, exits, injections;
static unsigned last_seed;
static int probe_execute(int cycles) {
    if (warm && !restored && g_cur_frame==warm_at) {
        restored=1; CHECK(load_state(warm)); g_frame_cycle=0; update_ipl();
        g_mouse_x=g_mouse_y=0x80;
#if MOON_DEVELOPER
        apply_script(g_cur_frame);
#endif
    }
    return m68k_execute(cycles);
}
void moon_instr_hook(unsigned pc) {
    unsigned before=m68k_get_reg(NULL,M68K_REG_D1);
    if (unfixed) g_tavern_focused=0;
    production_hook(pc);
    if (g_lineage!=LIN_CRACKED || !g_log) return;
    if (pc==0x2b650) {
        polls++;
        if (!(before&16) && (m68k_get_reg(NULL,M68K_REG_D1)&16)) {
            injections++;
            fprintf(g_log,"TI INJECT fr=%d\n",g_cur_frame);
            if (capture && !captured) { CHECK(save_state(capture)); captured=1; }
        }
    }
    if (pc==0x2b6cc) {
        bets++;
        fprintf(g_log,"TI BET fr=%d stake=%u gold=%u\n",g_cur_frame,r16(0x42f8e),r16(r32(0x42f66)+0x4a));
    }
    if (pc==0x2b722) { rolls++; fprintf(g_log,"TI ROLL fr=%d seed=%08x\n",g_cur_frame,r32(0x391a8)); }
    if (pc==0x2b740) fprintf(g_log,"TI FACES fr=%d faces=%u,%u,%u seed=%08x\n",g_cur_frame,
        r8(0x42f8a),r8(0x42f8b),r8(0x42f8c),r32(0x391a8));
    if (pc==0x2b2ca) { rng++; last_seed=m68k_get_reg(NULL,M68K_REG_D0); }
    if (pc==0x2b8c0) acknowledgements++;
    if (pc==0x2b5f4) exits++;
}

static uint8_t image[RAM_SIZE], expected[RAM_SIZE];
static void read_image(const char *path) {
    FILE *f=fopen(path,"rb"); CHECK(f);
    CHECK(fread(image,1,RAM_SIZE,f)==RAM_SIZE); CHECK(fclose(f)==0);
}
static void call(unsigned pc) {
    m68k_set_reg(M68K_REG_SR,0x2700); m68k_set_reg(M68K_REG_PC,pc);
    m68k_set_reg(M68K_REG_A7,0x1ff000); w32(0x1ff000,0x1ef000);
    g_stop=g_unmapped=0; m68k_set_irq(0); g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;
}
static void until(unsigned a,unsigned b) {
    unsigned count=0;
    while (m68k_get_reg(NULL,M68K_REG_PC)!=a && m68k_get_reg(NULL,M68K_REG_PC)!=b) {
        CHECK(++count<100000 && !g_stop && !g_unmapped); m68k_execute(1);
    }
}
static unsigned model(unsigned s) { return (s>>8) | (((s>>1)^(s>>4))<<24); }
static void native_tests(const char *port,const char *retail) {
    unsigned scopes=0,dice=0,payouts=0;
    for (int edition=0;edition<2;edition++) {
        read_image(edition?retail:port); memcpy(g_ram,image,RAM_SIZE);
        g_os=1; g_sdl_mode=0; g_mp_campaign=g_mp_practice=0;
        g_lineage=edition?LIN_RETAIL:LIN_CRACKED; g_tavern_focused=1;
        unsigned cb=edition?0x2b512:0x2b63a, sample=edition?0x2d49a:0x2d6c8, poll=cb+0x16;
        unsigned actor=edition?0x2e5b4:0x2e7dc, entry=edition?0x2b1f0:0x2b318;
        unsigned state=r32(cb+8), exiting=r32(cb+0x6e), holder=r32(cb+0x80), xy=r32(cb+0x42);
        unsigned hit=r32(cb+0x4e), list=r32(hit+0x12), root=0;
        for(unsigned at=0x21000;at<0x30000;at+=2)
            if(r16(at)==0x4eb9 && r32(at+2)==entry) { root=at+6; break; }
        CHECK(root); call(sample); w32(0x1ff000,root); w16(state,0); w16(exiting,0); w32(holder,actor);
        w32(actor+0x36,0); w32(list,0x1e0000);
        for(unsigned i=0;i<6;i++) {
            static const unsigned tops[]={38,66,94,121,147,181}; unsigned h=0x1e0000+24*i;
            w16(h+4,i<5?43:45);w16(h+6,i<5?24:16);w16(h+12,268);w16(h+14,tops[i]);
            w32(h+16,i<5?1:2);w16(h+20,i+1);
        }
        memcpy(expected,g_ram,RAM_SIZE);
        for(unsigned row=0;row<6;row++) for(unsigned flags=0;flags<32;flags++) {
            memcpy(g_ram,expected,RAM_SIZE);tavern_input_reset();
            unsigned h=0x1e0000+24*row;w16(xy,280);w16(xy+2,r16(h+14)+5);
            m68k_set_reg(M68K_REG_PC,sample);m68k_set_reg(M68K_REG_D1,16);tavern_input_hook(sample);
            CHECK(g_tavern_input.target==h);
            m68k_set_reg(M68K_REG_D1,0);tavern_input_hook(sample);
            unsigned regs[SAVE_NREGS];for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
            m68k_set_reg(M68K_REG_PC,poll);m68k_set_reg(M68K_REG_D1,0x12340000|flags);
            unsigned sr=m68k_get_reg(NULL,M68K_REG_SR);tavern_input_hook(poll);
            CHECK(m68k_get_reg(NULL,M68K_REG_D1)==(0x12340000|flags|16));
            CHECK(m68k_get_reg(NULL,M68K_REG_SR)==sr && !g_tavern_input.target);
            for(unsigned i=0;i<SAVE_NREGS;i++)if(SAVE_REGS[i]!=M68K_REG_PC && SAVE_REGS[i]!=M68K_REG_D1)
                CHECK(m68k_get_reg(NULL,SAVE_REGS[i])==regs[i]);
            m68k_set_reg(M68K_REG_D1,0);tavern_input_hook(poll);CHECK(!m68k_get_reg(NULL,M68K_REG_D1));
            scopes++;
        }
        /* Independently check rectangle edges/gaps against the original
         * hotspot routine, including its inclusive right/bottom boundaries. */
        for(unsigned row=0;row<6;row++)for(unsigned ix=0;ix<4;ix++)for(unsigned iy=0;iy<4;iy++) {
            memcpy(g_ram,expected,RAM_SIZE);unsigned h=0x1e0000+24*row;
            unsigned xs[]={267,268,268+r16(h+4),269+r16(h+4)};
            unsigned ys[]={r16(h+14)-1,r16(h+14),r16(h+14)+r16(h+6),r16(h+14)+r16(h+6)+1};
            w16(xy,xs[ix]);w16(xy+2,ys[iy]);unsigned target=tavern_input_target();
            call(hit);m68k_set_reg(M68K_REG_D0,xs[ix]);m68k_set_reg(M68K_REG_D1,ys[iy]);
            m68k_set_reg(M68K_REG_D2,0);m68k_set_reg(M68K_REG_D3,0);until(0x1ef000,0x1ef000);
            CHECK(target==(m68k_get_reg(NULL,M68K_REG_D0)?m68k_get_reg(NULL,M68K_REG_A0):0));scopes++;
        }
        for(int condition=0;condition<14;condition++) {
            memcpy(g_ram,expected,RAM_SIZE);g_os=1;g_tavern_focused=1;g_mp_campaign=g_mp_practice=0;
            call(sample);w32(0x1ff000,root);tavern_input_reset();w16(xy,280);w16(xy+2,104);
            m68k_set_reg(M68K_REG_D1,16);tavern_input_hook(sample);CHECK(g_tavern_input.target);
            switch(condition) {
                case 0:w16(state,1);break; case 1:w16(state,2);break; case 2:w16(exiting,1);break;
                case 3:w32(0x1ff000,0x1ef000);break; case 4:w32(holder,actor+4);break;
                case 5:w32(actor+0x36,4);break; case 6:g_os=0;break;case 7:g_mp_practice=1;break;
                case 8:g_tavern_focused=0;break;case 9:w16(xy+2,40);break;
                case 10:mp_clear_host_input();break;case 11:tavern_input_reset();break;
                case 12:g_mp_campaign=1;g_mp.phase=MP_CLAIM;g_mp_ui_player=0;g_mp.players=4;break;
                case 13:g_mp_campaign=1;g_mp.phase=MP_PLAY;g_mp_ui_player=1;g_mp.players=4;break;
            }
            m68k_set_reg(M68K_REG_PC,poll);m68k_set_reg(M68K_REG_D1,0);tavern_input_hook(poll);
            CHECK(!m68k_get_reg(NULL,M68K_REG_D1));scopes++;
        }
        /* Execute the real original RNG and payout code for both editions.
         * The expected arithmetic/table classification is independent of hooks. */
        memcpy(g_ram,image,RAM_SIZE);g_os=1;g_tavern_focused=1;g_mp_campaign=g_mp_practice=0;
        unsigned rngpc=edition?0x2b188:0x2b2b0, seed=r32(rngpc+6);
        for(unsigned s=1;s<=4096;s++) {
            unsigned value=s*0x9e3779b9u;w32(seed,value);call(rngpc);until(0x1ef000,0x1ef000);
            CHECK(r32(seed)==model(value) && m68k_get_reg(NULL,M68K_REG_D0)==model(value));dice++;
        }
        unsigned faces=r32((edition?0x2b5fa:0x2b722)+2), sort=edition?0x2b7bc:0x2b8e4;
        unsigned lookup=edition?0x2b69a:0x2b7c2, win=edition?0x2b714:0x2b83c, loss=edition?0x2b6bc:0x2b7e4;
        for(unsigned a=0;a<6;a++)for(unsigned b=0;b<6;b++)for(unsigned c=0;c<6;c++) {
            w8(faces,a);w8(faces+1,b);w8(faces+2,c);w8(faces+3,0);
            call(sort);until(0x1ef000,0x1ef000);call(lookup);until(win,loss);
            int won=(a==b && b==c) || ((a==0)+(b==0)+(c==0)==2);
            CHECK((m68k_get_reg(NULL,M68K_REG_PC)==win)==won);payouts++;
        }
    }
    printf("PASS native scopes=%u RNG=%u payout-results=%u profile=%s\n",scopes,dice,payouts,MOON_DEVELOPER?"development":"release");
}

int main(int argc,char **argv) {
    int n=1,log=0;const char *port=NULL,*retail=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--unfixed"))unfixed=1;
        else if(!strcmp(argv[i],"--capture-injection") && i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--warm") && i+2<argc){warm_at=atoi(argv[++i]);warm=argv[++i];}
        else if(!strcmp(argv[i],"--native") && i+2<argc){port=argv[++i];retail=argv[++i];}
        else {if(!strcmp(argv[i],"--log") && i+1<argc)log=1;argv[n++]=argv[i];}
    }
    CHECK(log);argv[n]=NULL;
    if(port) {m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);native_tests(port,retail);return 0;}
    int rc=moonstone_main(n,argv);
    printf("TI SUMMARY polls=%d bets=%d rolls=%d rng=%d seed=%08x acks=%d exits=%d injections=%d\n",
        polls,bets,rolls,rng,last_seed,acknowledgements,exits,injections);
    return rc;
}
