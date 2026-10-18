/* Private SDL keyboard/controller ownership and interrupted-tap regression. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static SDL_Window *make_window(const char*,int,int,int,int,Uint32);
static void present(SDL_Renderer*);
static const Uint8 *probe_keyboard(int *n) { if(n)*n=SDL_NUM_SCANCODES;return keys; }
static SDL_bool controllers(int i) { return SDL_JoystickIsVirtual(i)&&SDL_IsGameController(i); }
#define SDL_GetKeyboardState probe_keyboard
#define SDL_IsGameController controllers
#define SDL_CreateWindow make_window
#define SDL_RenderPresent present
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef SDL_GetKeyboardState
#undef SDL_IsGameController
#undef SDL_CreateWindow
#undef SDL_RenderPresent
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL SDL %d: %s\n",__LINE__,#x);exit(86);} }while(0)

static SDL_Joystick *joy[2];
static const char *fixture,*output;
static int owner,keyboard_owner,mode,presents,stage,at,last_poll=-100,bet_count,injections,done;
static char resume[1400];
static void attach(int i) {
    SDL_VirtualJoystickDesc d={0};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
    d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
    d.name="Moonstone tavern regression controller";
    int index=SDL_JoystickAttachVirtualEx(&d);CHECK(index>=0);joy[i]=SDL_JoystickOpen(index);CHECK(joy[i]);
}
static void focus(SDL_Window *win,int focused) {
    SDL_Event e={0};e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(win);
    e.window.event=focused?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;CHECK(SDL_PushEvent(&e)==1);
}
static void fire(int down) {
    if(keyboard_owner)keys[SDL_SCANCODE_LCTRL]=(Uint8)down;
    else CHECK(SDL_JoystickSetVirtualButton(joy[0],SDL_CONTROLLER_BUTTON_A,(Uint8)down)==0);
}
static SDL_Window *make_window(const char*t,int x,int y,int w,int h,Uint32 flags) {
    g_sdl_mode=0;g_bootphase=0;CHECK(load_state(fixture));
    unsigned from=r32(0x42f66),actor=0x2e7dc+0x84*owner;
    if(actor!=from)memcpy(g_ram+actor,g_ram+from,0x84);
    w32(actor+0x36,(unsigned)owner);w32(0x42f66,actor);w32(0x2e0bc,actor);w32(0x2fb08,actor);
    w16(0x2e024,4);w16(0x3051e,3);g_sdl_mode=1;
    SDL_Window *win=SDL_CreateWindow(t,x,y,w,h,flags|SDL_WINDOW_HIDDEN);CHECK(win);return win;
}
void moon_instr_hook(unsigned pc) {
    unsigned before=m68k_get_reg(NULL,M68K_REG_D1);production_hook(pc);
    if(pc==0x2b650) {
        last_poll=g_cur_frame;
        if(!(before&16) && (m68k_get_reg(NULL,M68K_REG_D1)&16))injections++;
    }
    if(pc==0x2b6cc) {bet_count++;CHECK(g_mp_ui_player==owner);}
}
static void present(SDL_Renderer *ren) {
    presents++;CHECK(presents<300);SDL_Window *win=SDL_RenderGetWindow(ren);
    if(presents==1) {
        focus(win,1);mp_reset(&g_mp);g_mp.players=4;g_mp.phase=MP_PLAY;g_mp.required=1u<<owner;
        g_mp.shared=1;g_mp.enrolled=15;
        for(int p=0;p<4;p++)g_mp.chosen[p]=SDL_JoystickInstanceID(joy[1]);
        g_mp.chosen[owner]=g_mp.device[owner]=keyboard_owner?MP_KEYBOARD:SDL_JoystickInstanceID(joy[0]);
        g_mp.device[(owner+1)%4]=SDL_JoystickInstanceID(joy[1]);
        g_mp_campaign=1;g_mp_context=MP_CAM_UI;g_mp_ui_player=owner;g_mp_combatants=0;
        mp_clear_host_input();
        snprintf(resume,sizeof(resume),"%s/owner.sav",output);CHECK(save_state(resume));
    }
    if(presents==3) {
        CHECK(SDL_JoystickSetVirtualButton(joy[keyboard_owner?0:1],SDL_CONTROLLER_BUTTON_A,1)==0);
        if(!keyboard_owner)keys[SDL_SCANCODE_LCTRL]=1;
    }
    if(presents==8) {
        CHECK(SDL_JoystickSetVirtualButton(joy[keyboard_owner?0:1],SDL_CONTROLLER_BUTTON_A,0)==0);
        keys[SDL_SCANCODE_LCTRL]=0;
    }
    if(stage==0 && presents>22 && g_cur_frame==last_poll+1) {
        CHECK(!bet_count && !g_tavern_input.target);fire(1);stage=1;at=presents;
    } else if(stage==1 && presents==at+1) {
        fire(0);CHECK(g_tavern_input.target);stage=2;at=presents;
        if(mode==1)focus(win,0);
        if(mode==2)CHECK(load_state(resume));
        if(mode==3) {
            int index=-1;
            for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_JoystickGetDeviceInstanceID(i)==SDL_JoystickInstanceID(joy[0]))index=i;
            CHECK(index>=0 && SDL_JoystickDetachVirtual(index)==0);SDL_JoystickClose(joy[0]);joy[0]=NULL;
        }
    } else if(stage==2 && mode && presents==at+4) {
        CHECK(!g_tavern_input.target && !bet_count);
        if(mode==1)focus(win,1);
        if(mode==3)attach(0);
    } else if(stage==2 && mode==3 && presents==at+7) {
        CHECK(g_mp.phase==MP_CLAIM && g_mp.claim==owner);
        CHECK(SDL_JoystickSetVirtualButton(joy[0],SDL_CONTROLLER_BUTTON_START,1)==0);
    } else if(stage==2 && mode==3 && presents==at+8) {
        CHECK(SDL_JoystickSetVirtualButton(joy[0],SDL_CONTROLLER_BUTTON_START,0)==0);
    } else if(stage==2 && mode && presents>at+25 && g_mp.phase==MP_PLAY && g_cur_frame==last_poll+1) {
        CHECK(!bet_count && !g_tavern_input.target);mode=0;fire(1);stage=1;at=presents;
    }
    if(stage==2 && !mode && bet_count) {
        CHECK(bet_count==1 && injections==1 && !g_tavern_input.target && g_mp_ui_player==owner);
        printf("PASS SDL owner=P%d device=%s profile=%s one-bet injections=%d\n",owner+1,
            keyboard_owner?"keyboard":"controller",MOON_DEVELOPER?"development":"release",injections);
        CHECK(save_state(resume));done=1;SDL_Event e={0};e.type=SDL_QUIT;CHECK(SDL_PushEvent(&e)==1);stage=3;
    }
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int n=1,log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--fixture")&&i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--output")&&i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--owner")&&i+1<argc)owner=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--mode")&&i+1<argc)mode=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--keyboard"))keyboard_owner=1;
        else {if(!strcmp(argv[i],"--log"))log=1;argv[n++]=argv[i];}
    }
    CHECK(fixture && output && log && owner>=0 && owner<4);argv[n]=NULL;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    attach(0);attach(1);int rc=moonstone_main(n,argv);CHECK(done);return rc;
}
