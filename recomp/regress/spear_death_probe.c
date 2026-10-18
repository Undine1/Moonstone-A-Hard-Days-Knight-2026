/* Native spear execution lookup/recovery oracle. Test only; never distribute. */
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
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(86);} }while(0)
enum { HERO=0x1e2000,KILLER=0x1e4000,BLOCK=0x1e6000,WORK=0x1e8000,STACK=0x1ff000,STOP=0x1ef000 };
static uint8_t original[RAM_SIZE],initial[RAM_SIZE],expected[RAM_SIZE];
static unsigned edition,mode,table,kill,lookup,dispatch,caller,updater,hero;
static unsigned regs[SAVE_NREGS],root_changes,recoveries;
static int native=1;
static const char *seed,*captures,*warm;
static int seeded,captured,warm_frame=-1,warmed,life_count;
static void capture(unsigned bit,const char *name) {
    if(!captures || (captured&(1u<<bit)))return;
    char path[1400];snprintf(path,sizeof(path),"%s/%s.sav",captures,name);
    CHECK(save_state(path));captured|=1u<<bit;
}
void moon_instr_hook(unsigned pc) {
    if(native) {if(mode==1)spear_death_hook(pc);return;}
    if(seed && !seeded && pc==0x288c8) {
        /* Controlled pending fatal contact using the exact actor/display
         * recycling layout of the user's save. This is a private specimen,
         * not a reconstruction of their exact preceding button sequence. */
        seeded=1;unsigned h=0x2e7dc,k=0x13460e,d=0x2ee30;
        CHECK(r8(h+0x4d)==0xc && r8(k+0x4d)==0x20 && r32(d+0x18)==h);
        w32(h,1);w16(h+0x50,3);w32(h+0xe,0);w32(h+0x12,k);w16(h+0x40,0);
        w32(k+0xe,0);w32(k+0x12,0);w16(k+0x40,4);
        w8(d,1);w8(d+1,0);w8(0x2ed36+1,0);
        CHECK(save_state(seed));
    }
    if(pc==0x265c4)capture(0,"before-install");
    if(pc==0x265ca)capture(1,"after-install");
    if(pc==0x28a7c) {
        unsigned d=m68k_get_reg(NULL,M68K_REG_A1);
        if(r32(d+0x18)==0x13460e && r32(d+2)==0x32b90)capture(2,"before-execution");
    }
    if(pc==0x21380)capture(3,"before-round-end");
    if(pc==0x21434){life_count++;capture(4,"before-life-loss");fprintf(g_log,"SPEAR-TEST life fr=%d\n",g_cur_frame);}
    unsigned p=pc==0x288c8?r32(0x2edfe + 2):0;
    production_hook(pc);
    if(p && p!=r32(0x2edfe + 2))recoveries++;
    if(pc==0x28846 && m68k_get_reg(NULL,M68K_REG_PC)!=pc)root_changes++;
}
static int probe_execute(int cycles) {
    if(warm && !warmed && g_cur_frame==warm_frame) {
        warmed=1;CHECK(load_state(warm));g_frame_cycle=0;update_ipl();g_mouse_x=g_mouse_y=0x80;
#if MOON_DEVELOPER
        apply_script(g_cur_frame);
#endif
    }
    return m68k_execute(cycles);
}
static void cpu(unsigned pc) {
    g_os=1;g_mp_practice=0;g_lineage=edition?LIN_RETAIL:LIN_CRACKED;g_stop=g_unmapped=0;
    for(unsigned i=0;i<SAVE_NREGS;i++)m68k_set_reg(SAVE_REGS[i],0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);w32(STACK,STOP);
    m68k_set_reg(M68K_REG_A7,STACK);m68k_set_reg(M68K_REG_PC,pc);
}
static void finish(void) {
    unsigned n=0;while(m68k_get_reg(NULL,M68K_REG_PC)!=STOP){CHECK(++n<2000);m68k_execute(1);CHECK(!g_stop&&!g_unmapped);}
}
static void setup(unsigned live,unsigned stale,unsigned face) {
    memcpy(g_ram,original,RAM_SIZE);memset(g_ram+table,0,500);memset(g_ram+HERO,0,0x9000);
    for(unsigned i=0;i<10;i++) {w32(table+i*50+0x24,WORK+i*36);memset(g_ram+WORK+i*36,0x37,36);}
    w32(hero,1);w8(hero+0x4d,0xc);w16(hero+0x50,3);w8(hero+0xa,face);
    w32(hero+0x12,KILLER);w32(hero+0x32,BLOCK);w32(BLOCK+4,8);
    w32(KILLER,1);w8(KILLER+0x4d,0x20);w16(KILLER+0x50,10);
    w16(KILLER+0x40,4);w8(KILLER+0xa,4-face);
    w32(edition?0x2e9ac:0x2ebd0,hero);w32(edition?0x302cc:0x30518,0);
    unsigned d=table+50*live;w32(d+0x18,KILLER);w8(d,1);w8(d+1,1);w32(d+2,0x123456);
    for(unsigned i=0;i<stale;i++){d=table+i*50;w32(d+0x18,KILLER);w32(d+2,0xabcdef);}
}
static void execute_hit(void) {
    cpu(edition?0x2669c:0x2657e);m68k_set_reg(M68K_REG_A0,KILLER);m68k_set_reg(M68K_REG_A1,hero);finish();
}
static void native_pairs(void) {
    unsigned pairs=0;
    for(unsigned player=0;player<4;player++)for(unsigned live=1;live<10;live++)
    for(unsigned stale=0;stale<=live;stale++)for(unsigned face=1;face<=3;face+=2) {
        hero=HERO+player*0x84;setup(live,stale,face);memcpy(initial,g_ram,RAM_SIZE);
        /* Independent oracle runs the unmodified original installer with
         * non-owning discarded slots, then restores those unrelated owners. */
        for(unsigned i=0;i<stale;i++)w32(table+i*50+0x18,0);
        mode=0;execute_hit();
        for(unsigned i=0;i<stale;i++)w32(table+i*50+0x18,KILLER);
        CHECK(r32(table+live*50+2)==kill && !r16(hero+0x50));
        memcpy(expected,g_ram,RAM_SIZE);for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        memcpy(g_ram,initial,RAM_SIZE);mode=1;execute_hit();
        CHECK(!memcmp(expected,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
        if(stale) {
            memcpy(g_ram,initial,RAM_SIZE);mode=0;execute_hit();
            CHECK(r32(table+2)==kill && !r8(table) && r32(table+live*50+2)==0x123456);
        }
        pairs++;
    }
    printf("PASS edition=%u native/oracle fatal contacts=%u (4 knights, both facings, 0..9 stale records)\n",edition,pairs);
}
static void prepare_pending(void) {
    hero=HERO;setup(9,1,1);cpu(dispatch);
    w32(hero,0);w16(hero+0x50,0);w32(hero+0x12,0);
    w32(table+2,kill);w8(table+1,1);memset(g_ram+WORK,0,36);
    unsigned f=r32(0x21382);w32(f,hero);w32(f+4,hero);w8(f+8,1);w8(f+0x10,0);
    w32(table+50+0x18,hero);
}
static void unchanged_contacts(void) {
    unsigned count=0;
    for(unsigned player=0;player<4;player++)for(unsigned face=1;face<=3;face+=2)
    for(unsigned test=0;test<4;test++) {
        hero=HERO+player*0x84;setup(9,3,face);
        if(test<2)w16(hero+0x50,test?10:4); /* nonfatal hits */
        if(test==2)w32(edition?0x302cc:0x30518,1); /* gore disabled */
        if(test==3)w32(hero+0xe,KILLER); /* reciprocal contact */
        memcpy(initial,g_ram,RAM_SIZE);mode=0;execute_hit();
        memcpy(expected,g_ram,RAM_SIZE);for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        memcpy(g_ram,initial,RAM_SIZE);mode=1;execute_hit();
        CHECK(!memcmp(expected,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));count++;
    }
    printf("PASS edition=%u unchanged nonfatal/gore-disabled/reciprocal contacts=%u\n",edition,count);
}
static void recovery_oracle(void) {
    prepare_pending();memcpy(initial,g_ram,RAM_SIZE);
    /* Call the original updater on the correct performer. The only extra
     * write consumes the abandoned pending marker in its inactive slot. */
    w32(table+0x18,0);mode=0;cpu(updater);m68k_set_reg(M68K_REG_A0,kill);m68k_set_reg(M68K_REG_A1,KILLER);finish();
    w32(table+0x18,KILLER);w8(table+1,0);
    memcpy(expected,g_ram,RAM_SIZE);
    memcpy(g_ram,initial,RAM_SIZE);cpu(dispatch);
    /* CPU helper only touched this same sentinel stack in both cases. */
    memcpy(expected+STACK-128,g_ram+STACK-128,132);
    for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
    spear_death_hook(dispatch);CHECK(!memcmp(expected,g_ram,RAM_SIZE));
    for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
    spear_death_hook(dispatch);CHECK(!memcmp(expected,g_ram,RAM_SIZE));
    printf("PASS edition=%u pending recovery matches original installer, CPU preserved, idempotent\n",edition);
}
static void guard_cases(void) {
    unsigned count=0;
    for(unsigned flags=0;flags<32;flags++)for(unsigned test=0;test<29;test++) {
        prepare_pending();unsigned f=r32(0x21382),d=table+450,pc=dispatch;
        m68k_set_reg(M68K_REG_SR,0x2700|flags);
        switch(test) {
          case 0:g_os=0;break;case 1:g_lineage=LIN_UNKNOWN;break;case 2:g_lineage=edition?LIN_CRACKED:LIN_RETAIL;break;
          case 3:pc+=2;break;case 4:w16(lookup,0);break;case 5:w16(dispatch,0);break;
          case 6:w16(caller,0);break;case 7:w32(caller-4,0);break;case 8:w16(kill,0);break;
          case 9:w8(f+8,0);break;case 10:w8(f+0x10,0x23);break;case 11:w32(f+4,KILLER);break;
          case 12:w8(hero+0x4d,0x10);break;case 13:w32(hero,1);break;case 14:w16(hero+0x50,1);break;
          case 15:w8(table+50,1);break;case 16:w8(table,1);break;case 17:w8(table+1,0);break;
          case 18:w32(table+2,kill+2);break;case 19:w8(WORK,1);break;case 20:w32(table+0x24,0);break;
          case 21:w8(KILLER+0x4d,0x1c);break;case 22:w32(KILLER,0);break;case 23:w16(KILLER+0x50,0);break;
          case 24:w8(d,0);break;case 25:w16(d+0x30,1);break;case 26:w32(d+0x24,RAM_SIZE-34);break;
          case 27:w32(table+100+0x18,KILLER);w8(table+100,1);break;case 28:g_mp_practice=1;break;
        }
        memcpy(expected,g_ram,RAM_SIZE);for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        spear_death_hook(pc);CHECK(!memcmp(expected,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));count++;
    }
    /* Unrelated callers and nonmatching/active display records must retain
     * the original lookup, including intentional inactive-record uses. */
    for(unsigned flags=0;flags<32;flags++)for(unsigned test=0;test<13;test++) {
        hero=HERO;setup(9,1,1);cpu(lookup);m68k_set_reg(M68K_REG_SR,0x2700|flags);
        m68k_set_reg(M68K_REG_A0,kill);m68k_set_reg(M68K_REG_A6,table);m68k_set_reg(M68K_REG_D0,KILLER);
        w32(STACK+4,updater+6);w32(STACK+8,caller+6);
        switch(test) {
          case 0:w32(STACK+4,0x1234);break;case 1:w32(STACK+8,0x1234);break;
          case 2:m68k_set_reg(M68K_REG_A0,kill+2);break;case 3:w8(table,1);break;
          case 4:w32(table+0x18,hero);break;case 5:m68k_set_reg(M68K_REG_A6,table+1);break;
          case 6:m68k_set_reg(M68K_REG_A6,table+500);break;case 7:m68k_set_reg(M68K_REG_A7,STACK+1);break;
          case 8:m68k_set_reg(M68K_REG_A7,RAM_SIZE-8);break;case 9:w8(KILLER+0x4d,0);break;
          case 10:w8(table+450,0);break;case 11:w8(table+50,1);w32(table+50+0x18,KILLER);break;
          case 12:g_os=0;break;
        }
        memcpy(expected,g_ram,RAM_SIZE);for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        spear_death_hook(lookup);CHECK(!memcmp(expected,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));count++;
    }
    printf("PASS edition=%u scope/CCR guards=%u\n",edition,count);
}
int main(int argc,char **argv) {
    if(argc<2)return 2;
    if(strcmp(argv[1],"--native")) {
        native=0;int n=1,log=0;
        for(int i=1;i<argc;i++) {
            if(!strcmp(argv[i],"--seed")&&i+1<argc)seed=argv[++i];
            else if(!strcmp(argv[i],"--captures")&&i+1<argc)captures=argv[++i];
            else if(!strcmp(argv[i],"--warm")&&i+1<argc)warm=argv[++i];
            else if(!strcmp(argv[i],"--warm-frame")&&i+1<argc)warm_frame=atoi(argv[++i]);
            else {if(!strcmp(argv[i],"--log"))log=1;argv[n++]=argv[i];}
        }
        CHECK(log);argv[n]=NULL;int rc=moonstone_main(n,argv);
        printf("SPEAR-TEST root=%u recovery=%u lives=%d\n",root_changes,recoveries,life_count);return rc;
    }
    if(argc!=6 || strcmp(argv[4],"--log"))return 2;
    g_log=fopen(argv[5],"w");CHECK(g_log);m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    for(edition=0;edition<2;edition++) {
        FILE*f=fopen(argv[2+edition],"rb");CHECK(f && fread(original,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
        table=edition?0x2ea68:0x2eca0;kill=edition?0x32940:0x32b90;lookup=edition?0x287d8:0x28846;
        dispatch=edition?0x2885a:0x288c8;caller=edition?0x266e2:0x265c4;updater=edition?0x2873a:0x2877c;
        native_pairs();unchanged_contacts();recovery_oracle();guard_cases();
    }
    fclose(g_log);printf("PASS both original editions profile=%s\n",MOON_DEVELOPER?"development":"release");return 0;
}
