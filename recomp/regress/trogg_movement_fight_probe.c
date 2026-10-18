/* Private full-delay observation. Never deploy. No gameplay-state changes. */
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

static unsigned hero=0x2e7dc, last_actor, requested, allowed, bounded, terrain;
static int hit=-1, move=-1, poll=-1, handler_x, handler_y, capcount;
static const char *direction, *captures;
static unsigned victim, reaction_saved;
static const char *warm_fixture;
static int warm_frame=-1,warm_done;
static int probe_execute(int cycles) {
    if(warm_fixture && !warm_done && g_cur_frame==warm_frame) {
        warm_done=1;
        /* Restore between frames from an already running fight. The CLI's
         * --loadstate-at alone starts its warmup in the intro, not this fight. */
        assert(load_state(warm_fixture));g_frame_cycle=0;
        /* Physical mouse counters intentionally are not saved. Reproduce the
         * cold test's device input, rather than comparing a different JOY0
         * counter after 100 frames of synthetic directional mouse movement. */
        g_mouse_x=g_mouse_y=0x80;apply_script(g_cur_frame);
        fprintf(g_log,"FULL warm-restore fr=%d pc=%x\n",g_cur_frame,m68k_get_reg(NULL,M68K_REG_PC));
    }
    return m68k_execute(cycles);
}
static unsigned input(void) {return g_ji_rt|(g_ji_lf<<1)|(g_ji_dn<<2)|(g_ji_up<<3)|(g_fire2<<4);}
static void snapshot(const char *event) {
    if(!captures || capcount>=8)return;
    char path[1200];snprintf(path,sizeof(path),"%s/%02d-%s-fr%d.sav",captures,capcount++,event,g_cur_frame);
    assert(save_state(path));fprintf(g_log,"FULL snapshot fr=%d path=%s\n",g_cur_frame,path);
}
static void actor(unsigned a) {
    fprintf(g_log," a=%x kind=%x x=%d y=%d z=%d hp=%d act=%x in=%x out=%x bx=%d,%d by=%d,%d",
      a,r8(a+0x4d),(int16_t)r16(a+4),(int16_t)r16(a+8),(int16_t)r16(a+6),
      (int16_t)r16(a+0x50),r16(a+0x40),r32(a+0x12),r32(a+0xe),
      (int16_t)r16(a+0x3a),(int16_t)r16(a+0x3c),(int16_t)r16(a+0x70),(int16_t)r16(a+0x72));
}
static void set_input(void) {
    ScriptEv e={0};e.frame=g_cur_frame+1;
    unsigned target=m68k_get_reg(NULL,M68K_REG_A1);
    const char *s=direction;
    if(!strcmp(s,"away"))s=(int16_t)r16(target+4)<(int16_t)r16(hero+4)?"r":"l";
    for(;*s;s++) {if(*s=='r')e.r=1;if(*s=='l')e.l=1;if(*s=='u')e.u=1;if(*s=='d')e.d=1;if(*s=='f')e.fire=1;}
    assert(g_nscript<MAX_SCRIPT);g_script[g_nscript++]=e;
    fprintf(g_log,"FULL requested fr=%d r=%d l=%d u=%d d=%d f=%d\n",e.frame,e.r,e.l,e.u,e.d,e.fire);
}
void moon_instr_hook(unsigned pc) {
    if(g_os && g_lineage==LIN_CRACKED && g_log) {
        if(pc==0x29772) {
            unsigned a=m68k_get_reg(NULL,M68K_REG_A0),b=m68k_get_reg(NULL,M68K_REG_A1);
            if(a==hero || b==hero) {
                fprintf(g_log,"FULL contact fr=%d source=%x target=%x\n",g_cur_frame,a,b);
                if(a==hero && hit<0) {hit=g_cur_frame;victim=b;if(direction)set_input();snapshot("hit");}
            }
        }
        if(pc==0x28892) {
            fprintf(g_log,"FULL loop fr=%d input=%x pace=%u",g_cur_frame,input(),r16(0x2dffa));actor(hero);fputc('\n',g_log);
            for(unsigned i=0;i<10;i++) {
                unsigned d=0x2eca0+50*i,a=r32(d+0x18);
                if(r8(d)) {
                    fprintf(g_log,"FULL display fr=%d slot=%u active=%u script=%x",g_cur_frame,i,r8(d+1),r32(d+2));
                    actor(a);fputc('\n',g_log);
                }
            }
        }
        if(pc==0x26118) {
            last_actor=m68k_get_reg(NULL,M68K_REG_A0);
            if(last_actor==hero) {
                handler_x=(int16_t)r16(hero+4);handler_y=(int16_t)r16(hero+8);
                fprintf(g_log,"FULL handler fr=%d",g_cur_frame);actor(hero);fputc('\n',g_log);
                if(hit>=0 && r32(hero+0x12))snapshot("incoming");
            }
        }
        if(pc==0x26144 && last_actor==hero) {
            requested=m68k_get_reg(NULL,M68K_REG_D0)&65535;
            fprintf(g_log,"FULL input fr=%d value=%x\n",g_cur_frame,requested);
            if(hit>=0) {if(poll<0)poll=g_cur_frame;snapshot("input");}
        }
        if(pc==0x26204) {allowed=m68k_get_reg(NULL,M68K_REG_D0)&65535;}
        if(pc==0x26226) {bounded=r16(hero+0x3e);}
        if(pc==0x2623e) {terrain=r16(hero+0x3e);}
        if(pc==0x26296 && last_actor==hero) {
            int x=(int16_t)r16(hero+4),y=(int16_t)r16(hero+8),changed=x!=handler_x||y!=handler_y;
            fprintf(g_log,"FULL movement fr=%d req=%x collision=%x bounds=%x terrain=%x dx=%d dy=%d hp=%d\n",
                g_cur_frame,requested,allowed,bounded,terrain,x-handler_x,y-handler_y,(int16_t)r16(hero+0x50));
            if(hit>=0 && changed && move<0)move=g_cur_frame;
        }
        if(pc==0x27ec8 && captures && hit>=0) {
            unsigned a=r32(0x2ebd0),bit=a==hero?1:a==victim?2:0;
            if(bit && !(reaction_saved&bit) && r32(a+0x12)) {
                char path[1200];snprintf(path,sizeof(path),"%s/reaction-%s-fr%d.sav",captures,a==hero?"hero":"victim",g_cur_frame);
                assert(save_state(path));reaction_saved|=bit;
                fprintf(g_log,"FULL reaction-snapshot fr=%d actor=%x script=%x path=%s\n",g_cur_frame,a,r32(0x2eaf8),path);
            }
        }
        if(pc==0x27ec8 && last_actor==hero && r32(0x2ebd0)==hero) {
            fprintf(g_log,"FULL selected fr=%d script=%x\n",g_cur_frame,r32(0x2eaf8));last_actor=0;
        }
        if(pc==0x26544 || pc==0x26df8) {
            fprintf(g_log,"FULL damage fr=%d actor=%x amount=%u\n",g_cur_frame,m68k_get_reg(NULL,M68K_REG_A1),m68k_get_reg(NULL,M68K_REG_D0)&65535);
        }
    }
    production_hook(pc);
}
int main(int argc,char **argv) {
    int n=1,log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--after-hit") && i+1<argc)direction=argv[++i];
        else if(!strcmp(argv[i],"--capture-dir") && i+1<argc)captures=argv[++i];
        else if(!strcmp(argv[i],"--warm-fixture") && i+1<argc)warm_fixture=argv[++i];
        else if(!strcmp(argv[i],"--warm-frame") && i+1<argc)warm_frame=atoi(argv[++i]);
        else {if(!strcmp(argv[i],"--log") && i+1<argc)log=1;argv[n++]=argv[i];}
    }
    assert(log);argv[n]=NULL;int rc=moonstone_main(n,argv);
    printf("FULL summary hit=%d poll=%d move=%d total=%d\n",hit,poll,move,move>=0?move-hit:-1);return rc;
}
