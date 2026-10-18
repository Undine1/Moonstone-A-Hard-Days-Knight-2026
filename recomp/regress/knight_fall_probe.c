/* Native death-animation and full-fight regression probe. Never distribute. */
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
#undef NDEBUG
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"assertion %d: %s\n",__LINE__,#x); exit(86); } } while(0)

enum { HERO=0x1e2000, ENEMY=0x1e4000, WORK=0x1e3000, STACK=0x1ff000, STOP=0x1ef000 };
static uint8_t initial[RAM_SIZE], expected[RAM_SIZE], snapshot[RAM_SIZE];
static unsigned saved_regs[SAVE_NREGS];
static int native, mode, edition, step, changes, life_calls, sounds;
static unsigned record, hero=HERO, slot;
static char parts[2048], trace[100000];
static const char *captures, *warm;
static int warm_at=-1, warm_done, capture_mask, fixes, last_lives=-1;
static unsigned entry(void) {return edition?0x289e8u:0x28a7cu;}
static unsigned table(void) {return edition?0x2ea68u:0x2eca0u;}
static unsigned tail(void) {return edition?0x31792u:0x319deu;}
static void append(const char *s) {assert(strlen(trace)+strlen(s)<sizeof(trace));strcat(trace,s);}
static void capture(unsigned bit,const char *name) {
    if(!captures || capture_mask&(1<<bit))return;
    char path[1400];snprintf(path,sizeof(path),"%s/%s.sav",captures,name);
    assert(save_state(path));capture_mask|=1<<bit;
}
void moon_instr_hook(unsigned pc) {
    unsigned d=m68k_get_reg(NULL,M68K_REG_A1);
    if(native) {
        if(pc==entry()) {
            /* Independent oracle: let the native pause-expiry code run with
             * one update remaining, instead of the host clearing its state.
             * Eligibility is supplied by the generated scenario, not by the
             * production helper's checks. */
            if(mode==2 && r32(d+2)==tail() && r8(WORK+1) && r32(WORK+2)==tail()-20)
                w8(WORK,1);
            unsigned before=r16(WORK);
            if(mode==1)knight_fall_hook(pc);
            if(before!=r16(WORK))changes++;
        }
        if(pc==(edition?0x28a4cu:0x28ae0u)) {
            unsigned p=m68k_get_reg(NULL,M68K_REG_A6);size_t n=strlen(parts);
            assert(n+18<sizeof(parts));
            snprintf(parts+n,sizeof(parts)-n,"%s%02x%02x%02x%02x%04x",n?"/":"",r8(p),r8(p+1),r8(p+2),r8(p+3),r16(p+4));
        }
        if(pc==(edition?0x3b1c0u:0x3b470u)) {
            char s[80];sounds++;snprintf(s,sizeof(s),"S:%d:%x\n",step,m68k_get_reg(NULL,M68K_REG_D0));append(s);
        }
        if(pc==0x21380u)life_calls++;
        return;
    }
    if(g_os && g_lineage==LIN_CRACKED) {
        if(pc==0x21434 || pc==0x21446) {
            unsigned a=m68k_get_reg(NULL,pc==0x21434?M68K_REG_A0:M68K_REG_A1);
            if(r8(a+0x4d)==0xc)
                fprintf(g_log,"DEATH subtract fr=%d actor=%x before=%u\n",g_cur_frame,a,r8(a+0x49));
        }
        if(pc==0x26554)capture(0,"before-selection");
        if(pc==0x2891c && m68k_get_reg(NULL,M68K_REG_A0)==0x319de)capture(1,"after-selection");
        if(pc==0x28a7c && d>=0x2eca0 && d<0x2eca0+500 && (d-0x2eca0)%50==0) {
            unsigned a=r32(d+0x18),s=r32(d+2);
            if(a && r8(a+0x4d)==0xc) {
                record=d;parts[0]=0;
                fprintf(g_log,"DEATH begin fr=%d actor=%x script=%x hp=%d lives=%u\n",g_cur_frame,a,s,(int16_t)r16(a+0x50),r8(a+0x49));
                if(last_lives>=0 && r8(a+0x49)!=(unsigned)last_lives)
                    fprintf(g_log,"DEATH lives fr=%d before=%d after=%u\n",g_cur_frame,last_lives,r8(a+0x49));
                last_lives=r8(a+0x49);
                if(s==0x319de)capture(2,"before-fall");
                if(s==0x31a00)capture(4,"mid-fall");
                if(s==0x31a22)capture(5,"corpse-hold");
            }
        }
        if(record && pc==0x28ae0) {
            unsigned p=m68k_get_reg(NULL,M68K_REG_A6);size_t n=strlen(parts);
            assert(n+18<sizeof(parts));
            snprintf(parts+n,sizeof(parts)-n,"%s%02x%02x%02x%02x%04x",n?"/":"",r8(p),r8(p+1),r8(p+2),r8(p+3),r16(p+4));
        }
        if(record && pc==0x28dca) {
            fprintf(g_log,"DEATH end fr=%d script=%x parts=%s\n",g_cur_frame,r32(record+2),parts);record=0;
        }
    }
    unsigned work=pc==0x28a7c && record?r32(record+0x24):0;
    unsigned before=work?r16(work):0;
    production_hook(pc);
    if(work && before!=r16(work)) {
        fixes++;fprintf(g_log,"DEATH corrected fr=%d work=%x before=%x after=%x\n",g_cur_frame,work,before,r16(work));
        capture(3,"after-pause-expired");
    }
}
static int probe_execute(int cycles) {
    if(warm && !warm_done && g_cur_frame==warm_at) {
        warm_done=1;assert(load_state(warm));g_frame_cycle=0;
        update_ipl(); /* The caller sampled IRQs before this test-only restore. */
        g_mouse_x=g_mouse_y=0x80;
#if MOON_DEVELOPER
        apply_script(g_cur_frame);
#endif
    }
    return m68k_execute(cycles);
}
static void begin(unsigned pc) {
    g_stop=g_unmapped=0;g_os=1;g_lineage=edition?LIN_RETAIL:LIN_CRACKED;
    for(unsigned i=0;i<SAVE_NREGS;i++)m68k_set_reg(SAVE_REGS[i],0);
    m68k_set_irq(0);m68k_set_reg(M68K_REG_SR,0x2700);
    m68k_set_reg(M68K_REG_A7,STACK);w32(STACK,STOP);m68k_set_reg(M68K_REG_PC,pc);
    g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;
}
static void finish(void) {
    unsigned n=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=STOP) {
        assert(++n<10000 && !g_stop && !g_unmapped);m68k_execute(1);
    }
}
static void run(int which,unsigned first,unsigned second,unsigned face,unsigned hp) {
    memcpy(g_ram,initial,RAM_SIZE);mode=which;changes=life_calls=sounds=0;trace[0]=0;
    unsigned d=table()+50*slot;
    if(slot) {memcpy(g_ram+d,g_ram+table(),50);memset(g_ram+table(),0,50);}
    if(hero!=HERO)memcpy(g_ram+hero,g_ram+HERO,0x84);
    w32(d+0x18,hero);w8(hero+0xa,face);w8(d+0x16,face);w16(hero+0x50,hp);
    for(step=1;step<=150 && r8(d+1);step++) {
        if((unsigned)step==first || (unsigned)step==second) {
            w32(hero+0x12,ENEMY);w32(edition?0x2e9ac:0x2ebd0,hero);
            begin(edition?0x26622:0x26504);m68k_set_reg(M68K_REG_A0,ENEMY);m68k_set_reg(M68K_REG_A1,hero);finish();
            assert(m68k_get_reg(NULL,M68K_REG_A0)==tail());
            unsigned scheduler=edition?0x288aa:0x28918;
            m68k_set_reg(M68K_REG_PC,scheduler);m68k_set_reg(M68K_REG_A7,STACK);w32(STACK,STOP);
            m68k_set_reg(M68K_REG_A6,d);m68k_set_reg(M68K_REG_D7,0);finish();
        }
        parts[0]=0;begin(entry());m68k_set_reg(M68K_REG_A1,d);finish();
        char s[2300];snprintf(s,sizeof(s),"%d:%x:%u:%u:%x:%s\n",step,r32(d+2),r8(WORK),r8(WORK+1),r32(WORK+2),parts);append(s);
    }
    assert(!r8(d+1));
}
static void pair(unsigned first,unsigned second,unsigned face,unsigned hp) {
    static char expected_trace[sizeof(trace)];
    run(2,first,second,face,hp);strcpy(expected_trace,trace);
    memcpy(expected,g_ram,RAM_SIZE);int expected_step=step,expected_sound=sounds,expected_life=life_calls;
    for(unsigned i=0;i<SAVE_NREGS;i++)saved_regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
    run(1,first,second,face,hp);
    assert(!strcmp(expected_trace,trace));assert(!memcmp(expected,g_ram,RAM_SIZE));
    for(unsigned i=0;i<SAVE_NREGS;i++)assert(saved_regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
    assert(step==expected_step && sounds==expected_sound && life_calls==expected_life);
    if(!changes) {
        run(0,first,second,face,hp);
        assert(!strcmp(expected_trace,trace) && !memcmp(expected,g_ram,RAM_SIZE));
    } else {
        /* Once the hit starts the final fall, the kneeling body parts must
         * never recur. Check visible native pose identifiers, not just cursor. */
        char *fall=strstr(trace,"08192a00ffcc");assert(fall && !strstr(fall,"08150701ffe4"));
    }
}
static void guards(void) {
    for(unsigned flags=0;flags<32;flags++)for(unsigned test=0;test<34;test++) {
        memcpy(g_ram,initial,RAM_SIZE);begin(entry());unsigned d=table(),pc=entry();
        m68k_set_reg(M68K_REG_A1,d);m68k_set_reg(M68K_REG_SR,0x2700|flags);
        w32(d+2,tail());w8(HERO+0x4d,0xc);w16(HERO+0x50,0);
        w16(WORK,0x1301);w32(WORK+2,tail()-20);
        /* Non-pause fields intentionally nonzero: none may be wiped. */
        memset(g_ram+WORK+6,0x37,30);
        switch(test) {
          case 1:g_os=0;break;case 2:g_lineage=LIN_UNKNOWN;break;
          case 3:g_lineage=edition?LIN_CRACKED:LIN_RETAIL;break;
          case 4:pc+=2;break;case 5:w16(pc,0x4e71);break;case 6:w16(pc+4,4);break;
          case 7:m68k_set_reg(M68K_REG_A1,d-50);break;
          case 8:m68k_set_reg(M68K_REG_A1,d+500);break;
          case 9:m68k_set_reg(M68K_REG_A1,d+1);break;
          case 10:w8(d,0);break;case 11:w8(d+1,0);break;
          case 12:w32(d+2,tail()+2);break;
          case 13:w16(tail()-24,0x4e71);break;case 14:w16(tail()-22,0x8813);break;
          case 15:w8(tail()-18,0);break;case 16:w8(tail()+1,9);break;
          case 17:w32(d+0x18,0);break;case 18:w32(d+0x18,HERO+1);break;
          case 19:w32(d+0x18,RAM_SIZE-0x80);break;case 20:w8(HERO+0x4d,0x1c);break;
          case 21:w16(HERO+0x50,1);break;case 22:w32(d+0x24,0);break;
          case 23:w32(d+0x24,WORK+1);break;case 24:w32(d+0x24,RAM_SIZE-34);break;
          case 25:w8(WORK+1,0);break;case 26:w8(WORK+1,2);break;
          case 27:w8(WORK,0);break;case 28:w8(WORK,21);break;
          case 29:w32(WORK+2,tail()+0x44);break;
          case 30:w16(HERO+0x50,0xffff);break;case 31:w8(HERO+0x4d,0x10);break;
          case 32:w8(WORK,1);break;case 33:w8(WORK,20);break;
        }
        memcpy(expected,g_ram,RAM_SIZE);
        for(unsigned i=0;i<SAVE_NREGS;i++)saved_regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        knight_fall_hook(pc);
        if(test==0 || test>=30) {expected[WORK]=expected[WORK+1]=0;}
        assert(!memcmp(expected,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)assert(saved_regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
        memcpy(snapshot,g_ram,RAM_SIZE);knight_fall_hook(pc);assert(!memcmp(snapshot,g_ram,RAM_SIZE));
    }
}
int main(int argc,char **argv) {
    const char *images=NULL,*log=NULL;int n=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--native") && i+1<argc)images=argv[++i];
        else if(!strcmp(argv[i],"--death-captures") && i+1<argc)captures=argv[++i];
        else if(!strcmp(argv[i],"--warm-fixture") && i+1<argc)warm=argv[++i];
        else if(!strcmp(argv[i],"--warm-frame") && i+1<argc)warm_at=atoi(argv[++i]);
        else {if(!strcmp(argv[i],"--log") && i+1<argc)log=argv[i+1];argv[n++]=argv[i];}
    }
    assert(log);argv[n]=NULL;
    if(!images) {int rc=moonstone_main(n,argv);printf("DEATH corrections=%d\n",fixes);return rc;}
    native=1;g_log=fopen(log,"w");assert(g_log);m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    unsigned pairs=0;
    for(edition=0;edition<2;edition++) {
        char path[1400];snprintf(path,sizeof(path),"%s/native-%d.ram",images,edition);
        FILE *f=fopen(path,"rb");assert(f && fread(initial,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
        guards();
        for(unsigned player=0;player<4;player++)for(unsigned facing=1;facing<=3;facing+=2) {
            hero=HERO+player*0x84;slot=player*3;
            for(unsigned hit=0;hit<=30;hit++) {pair(hit,0,facing,player&1?0xfff9:0);pairs++;}
            const unsigned hits[][2]={{6,7},{8,9},{8,13},{8,27},{23,24},{24,25},{26,27},{30,60}};
            for(unsigned i=0;i<sizeof(hits)/sizeof(hits[0]);i++) {pair(hits[i][0],hits[i][1],facing,0);pairs++;}
        }
    }
    fclose(g_log);printf("PASS: %u native fall/oracle pairs, both editions/facings, four knights; 2176 guard/CCR checks; complete RAM/CPU/poses/sounds agree\n",pairs);
    return 0;
}
