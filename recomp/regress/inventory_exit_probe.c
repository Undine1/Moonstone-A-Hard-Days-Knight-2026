/* Original 68k cleanup oracle plus real SDL inventory controls. Never deploy.
 * Every invocation requires an explicit scratch --log path. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <assert.h>
static Uint8 probe_keys[SDL_NUM_SCANCODES];
static int live, presents, live_exits, key_events;
static int owner, keyboard_owner, solo, variation;
static SDL_Joystick *pads[2];
static SDL_JoystickID pad_ids[2];
static unsigned char live_before[4][0x84], goods_before[4][24];
static unsigned goods[4];
static const char *out;
static const Uint8 *probe_keyboard(int *n) { if(n)*n=SDL_NUM_SCANCODES;return probe_keys; }
static SDL_Window *probe_window(const char *t,int x,int y,int w,int h,Uint32 f) {
    return SDL_CreateWindow(t,x,y,w,h,f|SDL_WINDOW_HIDDEN);
}
static SDL_bool probe_controller(int index) {
    return SDL_JoystickIsVirtual(index) && SDL_IsGameController(index);
}
static void probe_present(SDL_Renderer *ren);
#define SDL_GetKeyboardState probe_keyboard
#define SDL_CreateWindow probe_window
#define SDL_IsGameController probe_controller
#define SDL_RenderPresent probe_present
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef SDL_RenderPresent
#undef SDL_IsGameController
#undef SDL_CreateWindow
#undef SDL_GetKeyboardState
#undef NDEBUG
#include <assert.h>

enum { STACK=0x1ff000, STOP=0x1ef000, HOT=0x1d1000, SLOT=0x1d1100, TARGET=0x1d2000 };
typedef struct {
    unsigned loop,tail,cleanup,dispatch,hit,wait,tick,remove,blit,sfx;
    unsigned key,exitflag,cursor,slot,busy,wyrm,dragon,pair,scene;
} Layout;
static const Layout layouts[]={
    {0x2bc30,0x2bc6a,0x2bc6c,0x2cd7e,0x2a502,0x29f88,0x293c4,0x2d5f4,0x29b26,0x2cd6a,
     0x3c9a0,0x392e6,0x392c8,0x392da,0x3f534,0x2cfdc,0x2e9ec,0x2e0bc,0x2fb1c},
    {0x2bb0a,0x2bb62,0x2bb64,0x2cc4a,0x2a3be,0x29e3c,0x2929e,0x2d3c2,0x299da,0x2cc36,
     0x3c684,0x390a4,0x39088,0x39098,0x3f218,0x2cea6,0x2e7c4,0x2de94,0x2f8ca}
};
static int oracle,edition,click,cleanup_seen,sound_seen,continued;
static unsigned leaf_draw,leaf_sound;
static unsigned char originals[2][RAM_SIZE], expected[RAM_SIZE];
static const Layout *layout;

static void ret(void) {
    unsigned sp=m68k_get_reg(NULL,M68K_REG_A7);
    m68k_set_reg(M68K_REG_PC,r32(sp));m68k_set_reg(M68K_REG_A7,sp+4);
}
void moon_instr_hook(unsigned pc) {
    if(!oracle) {
        if(live && pc==0x2bca2 && presents>8) {
            for(int p=0;p<4;p++) {
                /* Closing must not buy, consume or undo anything. Compare at
                 * the inventory return, before its caller advances gameplay. */
                assert(!memcmp(g_ram+0x2e7dc+p*0x84+0x46,live_before[p]+0x46,0x1a));
                assert(!memcmp(g_ram+goods[p],goods_before[p],24));
            }
        }
        production_hook(pc);
        if(live && (pc==0x2bc6c || (pc==0x2bc30 && m68k_get_reg(NULL,M68K_REG_PC)==0x2bc6c)))live_exits++;
        return;
    }
    if(pc==layout->cleanup)cleanup_seen++;
    if(pc==layout->sfx)sound_seen++;
    if(pc==layout->wait) {
        /* A substituted leaf's return may execute the following branch in the
         * same Musashi step. Stop explicitly on the idle path instead. */
        continued=1;m68k_set_reg(M68K_REG_PC,STOP-2);return;
    }
    if(pc==layout->hit) {
        m68k_set_reg(M68K_REG_D0,click);m68k_set_reg(M68K_REG_A0,HOT);ret();return;
    }
    /* Only rendering/wait/audio leaves are substituted. The original keyboard
     * translator, menu branches, click dispatcher and cursor cleanup execute. */
    if(pc==layout->wait || pc==layout->tick || pc==layout->blit || pc==leaf_draw || pc==leaf_sound) {
        ret();return;
    }
    if(edition>=2) {
        production_hook(pc);
        if(pc==layout->loop && m68k_get_reg(NULL,M68K_REG_PC)==layout->cleanup)cleanup_seen++;
    }
}
static int run_case(int ver,unsigned scan,int use_click,int scene,int cursor,int wyrm,unsigned flags) {
    edition=ver;int ref=ver%2;layout=&layouts[ref];click=use_click;
    memcpy(g_ram,originals[ref],RAM_SIZE);
    g_os=ver>=2;g_lineage=ref?LIN_RETAIL:LIN_CRACKED;g_retail_parity=1;
    g_inventory_menu_active=1;g_inventory_close_request=ver>=2 && scan==45;
    g_sdl_mode=0;g_stop=0;g_icount=0;g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;
    cleanup_seen=sound_seen=continued=0;
    m68k_set_irq(0);m68k_set_reg(M68K_REG_SR,0x2700|flags);
    for(int i=0;i<15;i++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_A7,STACK);w32(STACK,STOP);w16(STOP-2,0x4e71);
    w16(layout->key,ver>=2?0:scan);w16(layout->exitflag,0);w16(layout->busy,1);
    unsigned cursor_slot=ver==2?0x3c096:SLOT;
    unsigned handler=ver==2&&cursor?0x2d61c:0x12345678;
    if(ver==2)memset(g_ram+0x3c096,0,0x24);
    w16(layout->cursor,cursor);w32(layout->slot,cursor_slot);w32(cursor_slot,handler);
    w16(layout->wyrm,wyrm);w32(layout->pair+4,TARGET);w32(layout->dragon+0x64,0x13572468);
    w32(layout->scene,scene);w32(HOT+0x10,7);
    /* The existing pointer debounce is a different flag, not the exit flag. */
    w16(ref?0x39096:0x392d8,0);
    leaf_draw=r32(layout->remove+26);leaf_sound=r32(layout->sfx+10);
    assert(leaf_draw<RAM_SIZE && leaf_sound<RAM_SIZE);
    m68k_set_reg(M68K_REG_PC,layout->loop);
    unsigned steps=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=STOP && m68k_get_reg(NULL,M68K_REG_PC)!=layout->tail) {
        if(++steps>5000){fprintf(stderr,"timeout edition=%d pc=%x scan=%u\n",ver,m68k_get_reg(NULL,M68K_REG_PC),scan);abort();}
        m68k_execute(1);assert(!g_stop);
    }
    int exited=!continued && m68k_get_reg(NULL,M68K_REG_PC)==STOP;
    assert(cleanup_seen==exited);
    if(exited) {
        assert(m68k_get_reg(NULL,M68K_REG_A7)==STACK+4);
        assert(!r16(layout->busy));assert(!r16(layout->cursor));
        assert(r32(cursor_slot)==(cursor?0:handler));assert(!r16(layout->wyrm));
        assert(r32(layout->dragon+0x64)==(wyrm?TARGET:0x13572468));
    } else {
        assert(r16(layout->busy)==1);assert(r16(layout->cursor)==cursor);
        assert(r32(cursor_slot)==handler);assert(r16(layout->wyrm)==wyrm);
    }
    assert(r32(layout->scene)==(unsigned)scene);
    return exited;
}
static void matrix(const char *dir) {
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);oracle=1;
    for(int i=0;i<2;i++) {
        char p[1200];snprintf(p,sizeof(p),"%s/%s.ram",dir,i?"retail":"port");
        FILE *f=fopen(p,"rb");assert(f);assert(fread(originals[i],1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
    unsigned key=45;
    assert(!run_case(0,key,0,0,1,0,0));
    assert(run_case(1,key,0,0,1,0,0));
    unsigned compare_cases=0,flags_cases=0;
    for(int sc=0;sc<12;sc++)for(int cur=0;cur<2;cur++)for(int wyrm=0;wyrm<2;wyrm++) {
        for(int v=1;v<4;v++) {
            assert(run_case(v,key,0,sc,cur,wyrm,0));assert(sound_seen==0);
            memcpy(expected,g_ram,RAM_SIZE);
            assert(run_case(v,0,1,sc,cur,wyrm,0));
            /* The raw key, click-only flag and inactive stack are the only
             * allowed differences from the native Exit button's outcome. */
            memcpy(expected+layout->key,g_ram+layout->key,2);
            memcpy(expected+layout->exitflag,g_ram+layout->exitflag,2);
            memcpy(expected+STACK-256,g_ram+STACK-256,260);
            assert(!memcmp(expected,g_ram,RAM_SIZE));
            compare_cases++;
        }
    }
    for(unsigned flags=0;flags<32;flags++)for(int v=0;v<4;v++) {
        assert(run_case(v,key,0,0,1,0,flags)==(v!=0));
        assert(run_case(v,0,1,0,1,0,flags));flags_cases+=2;
    }
    /* A malformed module or unknown lineage must not redirect execution. */
    memcpy(g_ram,originals[0],RAM_SIZE);g_os=1;g_lineage=LIN_UNKNOWN;
    m68k_set_reg(M68K_REG_PC,0x2bc30);g_inventory_close_request=1;
    inventory_close_hook(0x2bc30);assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2bc30);
    g_lineage=LIN_CRACKED;w16(0x2bc72,0x4e71);
    inventory_close_hook(0x2bc30);assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2bc30);
    inventory_close_hook(0x2bbec);assert(!g_inventory_close_request && !g_inventory_menu_active);
    g_inventory_close_request=1;mp_clear_host_input();assert(!g_inventory_close_request);
    printf("PASS native: %u complete cleanup comparisons, %u condition-code cases, signature/transient guards; retail X index=%u\n",compare_cases,flags_cases,key);
}
static void push_key(SDL_Scancode scan,int down,int repeat) {
    probe_keys[scan]=(Uint8)down;
    SDL_Event e={0};e.type=down?SDL_KEYDOWN:SDL_KEYUP;e.key.keysym.scancode=scan;
    e.key.keysym.sym=SDL_GetKeyFromScancode(scan);e.key.repeat=(Uint8)repeat;
    assert(SDL_PushEvent(&e)==1);key_events++;
}
static void button(int pad,int button,int down) {
    assert(SDL_JoystickSetVirtualButton(pads[pad],button,(Uint8)down)==0);
}
static void focus(SDL_Renderer *ren,int focused) {
    SDL_Event e={0};e.type=SDL_WINDOWEVENT;
    e.window.windowID=SDL_GetWindowID(SDL_RenderGetWindow(ren));
    e.window.event=focused?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;
    assert(SDL_PushEvent(&e)==1);
}
static void close_press(int down,int repeat) {
    if(keyboard_owner)push_key(variation==3?SDL_SCANCODE_C:SDL_SCANCODE_X,down,repeat);
    else button(0,variation==3?SDL_CONTROLLER_BUTTON_X:SDL_CONTROLLER_BUTTON_B,down);
}
static void probe_present(SDL_Renderer *ren) {
    static uint64_t first_icount;
    presents++;
    if(presents==1) {
        first_icount=g_icount;
        focus(ren,1);
        if(!solo)assert(g_mp.phase==MP_CLAIM && g_mp_ui_player==owner);
        if(!solo && keyboard_owner)push_key(SDL_SCANCODE_RETURN,1,0);
        else button(0,SDL_CONTROLLER_BUTTON_START,1);
    }
    if(presents==2) {
        if(!solo && keyboard_owner)push_key(SDL_SCANCODE_RETURN,0,0);
        else button(0,SDL_CONTROLLER_BUTTON_START,0);
    }
    if(presents==8) {
        assert(g_icount>first_icount && g_inventory_menu_active && !live_exits);
        assert(solo || (g_mp.phase==MP_PLAY && g_mp_ui_player==owner
            && mp_keyboard_gameplay()==keyboard_owner));
        for(int p=0;p<4;p++) {
            unsigned actor=0x2e7dc+p*0x84;
            memcpy(live_before[p],g_ram+actor,0x84);
            goods[p]=r32(actor+0x60);assert(goods[p]<RAM_SIZE-24);
            memcpy(goods_before[p],g_ram+goods[p],24);
        }
        char path[1200];snprintf(path,sizeof(path),"%s/open.sav",out);assert(save_state(path));
    }
    if(presents==10) {
        /* The idle pad and (in a pad-owned campaign) keyboard cannot cancel. */
        button(1,variation==3?SDL_CONTROLLER_BUTTON_X:SDL_CONTROLLER_BUTTON_B,1);
        if(!solo && !keyboard_owner)push_key(SDL_SCANCODE_X,1,0);
        if(variation==3) {
            button(0,SDL_CONTROLLER_BUTTON_B,1);
            if(keyboard_owner)push_key(SDL_SCANCODE_X,1,0);
        }
    }
    if(presents==14) {
        button(1,variation==3?SDL_CONTROLLER_BUTTON_X:SDL_CONTROLLER_BUTTON_B,0);
        if(!solo && !keyboard_owner)push_key(SDL_SCANCODE_X,0,0);
        if(variation==3) {
            button(0,SDL_CONTROLLER_BUTTON_B,0);
            if(keyboard_owner)push_key(SDL_SCANCODE_X,0,0);
        }
    }
    if(presents==18)assert(!live_exits && g_inventory_menu_active);
    if(variation==2) {
        if(presents==20){focus(ren,0);close_press(1,0);}
        if(presents==22){assert(!live_exits);close_press(0,0);}
        if(presents==23)focus(ren,1);
    }
    if(presents==24) {
        if(variation==5)push_key(SDL_SCANCODE_LCTRL,1,0);
        else close_press(1,0);
    }
    if(presents==29 && variation==5)push_key(SDL_SCANCODE_LCTRL,0,0);
    if(presents==38)assert(live_exits==(variation==4?0:1));
    if(presents==40) {
        /* A warm load clears a pending cancel and does not replay a held B/X. */
        char path[1200];snprintf(path,sizeof(path),"%s/open.sav",out);
        g_inventory_close_request=1;
        assert(load_state(path));assert(!g_inventory_close_request && !g_inventory_menu_active);
    }
    if(presents==45 && keyboard_owner && variation!=5)close_press(1,1);
    if(presents==50) {
        assert(g_inventory_menu_active && live_exits==(variation==4?0:1));
        assert(solo || (g_mp.phase==MP_PLAY && g_mp_ui_player==owner));
        close_press(0,0);
    }
    if(presents==54)close_press(1,0);
    if(presents==59)close_press(0,0);
    if(presents==80) {
        assert(g_icount>first_icount && live_exits==(variation==4?0:2));
        char path[1200];snprintf(path,sizeof(path),"%s/final.sav",out);assert(save_state(path));
        printf("PASS SDL owner=P%d keyboard=%d solo=%d variation=%d exits=%d frames=%d\n",owner+1,keyboard_owner,solo,variation,live_exits,presents);
        SDL_Event e={0};e.type=SDL_QUIT;assert(SDL_PushEvent(&e)==1);
    }
    assert(presents<=81);SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    const char *dir=NULL,*log=NULL;int n=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--exit-oracle") && i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--exit-live") && i+1<argc){out=argv[++i];live=1;}
        else if(!strcmp(argv[i],"--owner") && i+1<argc)owner=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--keyboard"))keyboard_owner=1;
        else if(!strcmp(argv[i],"--solo"))solo=1;
        else if(!strcmp(argv[i],"--variation") && i+1<argc)variation=atoi(argv[++i]);
        else {if(!strcmp(argv[i],"--log") && i+1<argc)log=argv[i+1];argv[n++]=argv[i];}
    }
    if(!log){fputs("Explicit scratch --log required\n",stderr);return 2;}
    argv[n]=NULL;
    if(dir){g_log=fopen(log,"w");assert(g_log);matrix(dir);fclose(g_log);return 0;}
    assert(live && out);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    for(int i=0;i<2;i++) {
        SDL_VirtualJoystickDesc desc={0};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
        desc.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;desc.name="Moonstone inventory test controller";
        int index=SDL_JoystickAttachVirtualEx(&desc);assert(index>=0);
        pads[i]=SDL_JoystickOpen(index);assert(pads[i]);pad_ids[i]=SDL_JoystickInstanceID(pads[i]);
    }
    return moonstone_main(n,argv);
}
