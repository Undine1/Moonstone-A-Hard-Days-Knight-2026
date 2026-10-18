/* Mudman spawn pixel/call-boundary probe. TEST ONLY; never distribute. */
#define SDL_MAIN_HANDLED
#include "../vendor/Musashi-master/m68k.h"
static int probe_execute(int cycles);
#define m68k_execute probe_execute
#define main moonstone_main
#define moon_instr_hook production_hook
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef m68k_execute
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(86);}}while(0)
static int kind=-1, weak, left, native, warm_at=-1, warmed;
static const char *capture,*warm;
static unsigned spawns, captures, last_frame=~0u;
static uint8_t initial[RAM_SIZE], baseline[RAM_SIZE], saved[RAM_SIZE];
static uint16_t custom[sizeof(g_custom)/sizeof(g_custom[0])];
static unsigned regs[SAVE_NREGS];

static void snap(unsigned bit,const char *name) {
    if(!capture || captures&(1u<<bit))return;
    char path[1400];snprintf(path,sizeof(path),"%s/%s.sav",capture,name);
    CHECK(save_state(path));captures|=1u<<bit;
}
void moon_instr_hook(unsigned pc) {
    if(native)return;
    if(g_os && g_lineage==LIN_CRACKED) {
        if(pc==0x25a10 && kind>=0) {
            w16(m68k_get_reg(NULL,M68K_REG_A0)+4,kind);kind=-1;
            if(left)w16(0x2e1ea,1);
        }
        if(pc==0x25840) {
            unsigned a=m68k_get_reg(NULL,M68K_REG_A1);
            if(weak)w16(a+0x50,1);
            spawns++;snap(0,"constructor");
        }
        if(pc==0x28a7c) {
            unsigned d=m68k_get_reg(NULL,M68K_REG_A1),a=r32(d+0x18);
            if(a && r8(a+0x4d)==4 && last_frame!=(unsigned)g_cur_frame) {
                last_frame=g_cur_frame;
                fprintf(g_log,"MUD fr=%d owner=%x xy=%d,%d hp=%d flags=%x script=%x hide=%x\n",g_cur_frame,a,
                        (int16_t)r16(a+4),(int16_t)r16(a+8),(int16_t)r16(a+0x50),r16(a+0x68),r32(d+2),r16(d+0x30));
                if(r32(d+2)==0x35d14)snap(1,"first-pose");
                if(r32(d+2)==0x35c4c)snap(3,"emergence");
            }
        }
        if(pc==0x3e99a) {
            /* At the instruction boundary PC has not yet become PPC. */
            unsigned sp=m68k_get_reg(NULL,M68K_REG_A7),d=r32(sp+4);
            if(r32(sp)==0x28c9c && d>=0x2eca0 && d<0x2ee94 && (d-0x2eca0)%50==0
               && r32(d+2)>=0x35d1a && r32(d+2)<=0x35d32
               && r8(r32(d+0x18)+0x4d)==4 && r16(r32(d+0x18)+0x68)==0)
                snap(2,"before-pixels");
        }
    }
    production_hook(pc);
}
static int probe_execute(int cycles) {
    if(warm && !warmed && g_cur_frame==warm_at) {
        warmed=1;CHECK(load_state(warm));g_frame_cycle=0;update_ipl();
        g_mouse_x=g_mouse_y=0x80;
#if MOON_DEVELOPER
        apply_script(g_cur_frame);
#endif
    }
    return m68k_execute(cycles);
}
enum { ACTOR=0x1e2000, STACK=0x1ff000, PIXELS=0x1e6000 };
static unsigned setup(int retail,unsigned slot,unsigned piece,int x,unsigned face) {
    memcpy(g_ram,initial,RAM_SIZE);g_os=1;g_lineage=retail?LIN_RETAIL:LIN_CRACKED;
    unsigned ret=retail?0x28bf0:0x28c9c,d=(retail?0x2ea68:0x2eca0)+slot*50;
    unsigned pose=retail?0x35aca:0x35d1a;
    memset(g_ram+d,0,50);memset(g_ram+ACTOR,0,0x84);
    w32(STACK,ret);w32(STACK+4,d);w8(d,1);w8(d+1,1);w32(d+2,pose+piece*6);
    w16(d+6,x);w8(d+0x16,face);w32(d+0x18,ACTOR);w32(ACTOR,1);
    w8(ACTOR+0x4d,4);w16(ACTOR+0x50,30);w16(ACTOR+4,x);
    m68k_set_reg(M68K_REG_A7,STACK);
    m68k_set_reg(M68K_REG_PPC,r32(ret-4)+0x3be);
    return d;
}
static void check_blit(void) {
    /* A real masked blit, not a duplicate of the implementation. */
    memset(g_custom,0,sizeof(g_custom));
    for(unsigned n=0;n<32;n++) {
        g_ram[PIXELS-96+n]=(uint8_t)(n*37+5);
        g_ram[PIXELS-64+n]=(uint8_t)(n*23+9);
        g_ram[PIXELS+n]=(uint8_t)(n*19+11);
    }
    g_custom[0x040/2]=0x0fca;g_custom[0x044/2]=g_custom[0x046/2]=0xffff;
    g_custom[0x058/2]=4*64+4;
    blt_setptr(0x050,PIXELS-96);blt_setptr(0x04c,PIXELS-64);
    blt_setptr(0x048,PIXELS);blt_setptr(0x054,PIXELS);
    memcpy(saved,g_ram,RAM_SIZE);memcpy(custom,g_custom,sizeof(custom));
    unsigned ret=r32(STACK);w32(STACK,ret+2);blitter_run();w32(STACK,ret);
    memcpy(baseline,g_ram,RAM_SIZE);uint16_t expected[sizeof(custom)/2];memcpy(expected,g_custom,sizeof(expected));
    memcpy(g_ram,saved,RAM_SIZE);memcpy(g_custom,custom,sizeof(custom));
    CHECK(mudman_spawn_blit_hidden());blitter_run();
    CHECK(!memcmp(g_ram,saved,RAM_SIZE));CHECK(!memcmp(g_custom,expected,sizeof(expected)));
    CHECK(memcmp(baseline+PIXELS,g_ram+PIXELS,32));
    memcpy(baseline+PIXELS,g_ram+PIXELS,32);CHECK(!memcmp(baseline,g_ram,RAM_SIZE));
}
static void check_native(const char *folder) {
    native=1;m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    unsigned positives=0,guards=0;
    for(int retail=0;retail<2;retail++) {
        char path[1400];snprintf(path,sizeof(path),"%s/%s.ram",folder,retail?"retail":"port");
        FILE*f=fopen(path,"rb");CHECK(f);CHECK(fread(initial,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
        for(unsigned slot=0;slot<10;slot++)for(unsigned part=0;part<5;part++)
        for(unsigned side=0;side<2;side++)for(unsigned face=1;face<=3;face+=2) {
            setup(retail,slot,part,side?340:-50,face);CHECK(mudman_spawn_blit_hidden());
            check_blit();positives++;
        }
        for(unsigned flags=0;flags<32;flags++)for(unsigned test=0;test<33;test++) {
            unsigned d=setup(retail,0,0,340,1),ret=r32(STACK),pose=r32(d+2);
            m68k_set_reg(M68K_REG_SR,0x2700|flags);
            switch(test) {
            case 0:g_os=0;break;case 1:g_lineage=LIN_UNKNOWN;break;
            case 2:g_lineage=retail?LIN_CRACKED:LIN_RETAIL;break;
            case 3:m68k_set_reg(M68K_REG_A7,STACK+1);break;
            case 4:m68k_set_reg(M68K_REG_A7,RAM_SIZE-6);break;
            case 5:w32(STACK,ret+2);break;case 6:w16(ret-6,0x4e71);break;
            case 7:w16(ret,0x4e71);break;case 8:w32(ret-4,RAM_SIZE-4);break;
            case 9:m68k_set_reg(M68K_REG_PPC,r32(ret-4));break;
            case 10:w16(r32(ret-4)+0x3be,0x4e71);break;
            case 11:w32(STACK+4,d-50);break;case 12:w32(STACK+4,d+500);break;
            case 13:w32(STACK+4,d+1);break;case 14:w8(d,0);break;case 15:w8(d+1,0);break;
            case 16:w16(d+0x30,1);break;case 17:w32(d+2,pose-6);break;
            case 18:w32(d+2,pose+30);break;case 19:w32(d+2,pose+1);break;
            case 20:w16(d+6,175);break;case 21:w16(d+6,319);break;
            case 22:w32(d+0x18,0);break;case 23:w32(d+0x18,ACTOR+1);break;
            case 24:w32(d+0x18,RAM_SIZE-0x80);break;case 25:w32(ACTOR,0);break;
            case 26:w8(ACTOR+0x4d,0x24);break;case 27:w8(ACTOR+0x4d,0xc);break;
            case 28:w16(ACTOR+0x50,0);break;case 29:w16(ACTOR+0x50,0xffff);break;
            case 30:w16(ACTOR+0x68,0x1800);break;case 31:w16(ACTOR+0x68,0x100);break;
            case 32:w8(pose+1,0);break;
            }
            memcpy(saved,g_ram,RAM_SIZE);
            for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
            CHECK(!mudman_spawn_blit_hidden());CHECK(!memcmp(saved,g_ram,RAM_SIZE));
            for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
            guards++;
        }
    }
    printf("PASS native: %u real blits, %u read-only scope guards, both editions\n",positives,guards);
}
int main(int argc,char **argv) {
    int n=1,log=0;const char *folder=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--native") && i+1<argc)folder=argv[++i];
        else if(!strcmp(argv[i],"--kind") && i+1<argc)kind=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--weak"))weak=1;
        else if(!strcmp(argv[i],"--left-spawn"))left=1;
        else if(!strcmp(argv[i],"--capture") && i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--warm-fixture") && i+1<argc)warm=argv[++i];
        else if(!strcmp(argv[i],"--warm-frame") && i+1<argc)warm_at=atoi(argv[++i]);
        else {if(!strcmp(argv[i],"--log"))log=1;argv[n++]=argv[i];}
    }
    CHECK(log);if(folder){check_native(folder);return 0;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);printf("MUD spawns=%u\n",spawns);return rc;
}
