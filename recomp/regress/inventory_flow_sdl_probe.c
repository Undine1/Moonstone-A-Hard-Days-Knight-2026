/* Investigation only. Never deploy. Real SDL menu/cast/return observations. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static int presents,stage,stage_at,release_at,click_at,loops,returned,exits,entries,menu_ready;
static int slot=14,close_key,owner,keyboard_owner=1,action,rotations,warm,resume;
static unsigned checkpoint;
static int checkpoint_saved,checkpoint_reload,finish_only;
static const char *out;
static SDL_Joystick *joy[2];
static const Uint8 *probe_keyboard(int *n) {if(n)*n=SDL_NUM_SCANCODES;return keys;}
static SDL_Window *probe_window(const char *t,int x,int y,int w,int h,Uint32 f) {
    return SDL_CreateWindow(t,x,y,w,h,f|SDL_WINDOW_HIDDEN);
}
static SDL_bool probe_controller(int index) {return SDL_JoystickIsVirtual(index)&&SDL_IsGameController(index);}
static void present(SDL_Renderer *ren);
#define SDL_GetKeyboardState probe_keyboard
#define SDL_CreateWindow probe_window
#define SDL_IsGameController probe_controller
#define SDL_RenderPresent present
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

static void state(const char *tag) {
    unsigned actor=r32(0x2fb08),inv=r32(actor+0x60);
    fprintf(g_log,"SCROLL-OBS %s present=%d stage=%d pc=%x sp=%x mode=%u previous=%u actor=%x target=%x count=%u cursor=%u exit=%u wyrm=%u dragon=%x ui=P%d phase=%d entries=%d cleanups=%d\n",
        tag,presents,stage,m68k_get_reg(NULL,M68K_REG_PC),m68k_get_reg(NULL,M68K_REG_SP),
        r32(0x2fb1c),r32(0x2fb04),actor,r32(0x2fb10),r8(inv+slot),r16(0x392c8),r16(0x392e6),
        r16(0x2cfdc),r32(0x2e9ec+0x64),g_mp_ui_player+1,g_mp.phase,entries,exits);
    fflush(g_log);
}
void moon_instr_hook(unsigned pc) {
    if(checkpoint && !checkpoint_saved && stage>=1 && pc==checkpoint
        && (pc==0x2cf18 || pc==0x2cf96 || r32(0x2fb1c)==8 || r32(0x2fb1c)==11)) {
        char path[1400];snprintf(path,sizeof(path),"%s/boundary.sav",out);
        checkpoint_saved=1;assert(save_state(path));
        if(checkpoint_reload)assert(load_state(path));
    }
    if(pc==0x1ef000 && !returned) {returned=1;state("returned");}
    if(pc==0x2bbec){entries++;menu_ready=0;state("entry");}
    if(pc==0x2cf18 || pc==0x2cf96)menu_ready=0;
    if(pc==0x2bca4)menu_ready=0;
    if(pc==0x2bc30){loops++;menu_ready=1;}
    if(pc==0x2bc6c){exits++;state("cleanup");}
    if(pc==0x2cd52)state("target-change");
    if(pc==0x2bddc)state("restore-mode");
    production_hook(pc);
    if(pc==0x2bc30 && m68k_get_reg(NULL,M68K_REG_PC)==0x2bc6c){exits++;state("shortcut-cleanup");}
}
static void key(SDL_Scancode sc,int down,int repeat) {
    keys[sc]=(Uint8)down;SDL_Event e={0};e.type=down?SDL_KEYDOWN:SDL_KEYUP;
    e.key.keysym.scancode=sc;e.key.keysym.sym=SDL_GetKeyFromScancode(sc);e.key.repeat=(Uint8)repeat;
    assert(SDL_PushEvent(&e)==1);
}
static void pad(int p,int b,int down) {assert(SDL_JoystickSetVirtualButton(joy[p],b,(Uint8)down)==0);}
static void fire(int down) {
    if(keyboard_owner)key(SDL_SCANCODE_LCTRL,down,0);else pad(0,SDL_CONTROLLER_BUTTON_A,down);
}
static void close_input(int down) {
    if(keyboard_owner)key(SDL_SCANCODE_X,down,0);else pad(0,SDL_CONTROLLER_BUTTON_B,down);
}
static unsigned hotspot(int kind,int field,int special) {
    unsigned base=r32(0x3a96c);
    for(int n=0;n<500;n++) {
        unsigned h=base+24*n;if(h>RAM_SIZE-24 || !r16(h+4))break;
        if(special==1 ? r32(h+16)==7 : special==2 ? r32(h+8)==0x39bec
            : special==3 ? r16(h+20)==5 && r16(h+22)==0 && r16(h+12)>150
            : r16(h+20)==kind && r16(h+22)==field) return h;
    }
    return 0;
}
static void click(unsigned h) {
    assert(h);unsigned label=r32(h+8);unsigned text=r32(label);
    fprintf(g_log,"SCROLL-CLICK hotspot=%x kind=%x arg=%x flags=%x text=%.*s\n",
        h,r16(h+20),r16(h+22),r16(label+8),text<RAM_SIZE?100:0,g_ram+text);
    w16(0x392d4,r16(h+12)+2);w16(0x392d6,r16(h+14)+2);
    /* Let the real pointer task and any already-loaded D0/D1 coordinates catch
     * up before pressing fire. Same-frame pointer writes can click the old row. */
    click_at=presents+3;
}
static void advance(int next) {stage=next;stage_at=presents;state("stage");}
static void snapshot(SDL_Renderer *ren,const char *name) {
    char p[1400];snprintf(p,sizeof(p),"%s/%s.sav",out,name);assert(save_state(p));
    int w,h;assert(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
    assert(SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    snprintf(p,sizeof(p),"%s/%s.bmp",out,name);assert(SDL_SaveBMP(s,p)==0);SDL_FreeSurface(s);
}
static void present(SDL_Renderer *ren) {
    presents++;
    if(presents==1) {
        SDL_Event e={0};e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(SDL_RenderGetWindow(ren));
        e.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;assert(SDL_PushEvent(&e)==1);
    }
    if(g_mp.phase==MP_CLAIM && stage==0 && presents==2) {
        assert(g_mp_ui_player==owner);
        if(keyboard_owner)key(SDL_SCANCODE_RETURN,1,0);else pad(0,SDL_CONTROLLER_BUTTON_START,1);
    }
    if(presents==4) {
        if(keyboard_owner)key(SDL_SCANCODE_RETURN,0,0);else pad(0,SDL_CONTROLLER_BUTTON_START,0);
    }
    if(release_at && presents==release_at){fire(0);close_input(0);release_at=0;}
    if(click_at && presents==click_at){fire(1);release_at=presents+3;click_at=0;}
    if(!stage && finish_only && presents>15 && g_mp.phase==MP_PLAY) {
        advance(5);
    } else if(!stage && resume && presents>15 && g_mp.phase==MP_PLAY && menu_ready) {
        assert(r32(0x2fb1c)==(slot==14?8u:11u) || (slot==14 && r32(0x2fb1c)==9));
        snapshot(ren,"before");advance(2);
    } else if(!stage && presents>15 && g_mp.phase==MP_PLAY && loops>2 && r32(0x2fb1c)==9) {
        snapshot(ren,"before");unsigned h=hotspot(5,slot,0);assert(h);
        assert(r16(r32(h+8)+8)&16);click(h);advance(1);
    } else if(stage==1 && menu_ready && presents-stage_at>12 && r32(0x2fb1c)==(slot==14?8u:11u)) {
        assert(g_mp.phase==MP_PLAY && g_mp_ui_player==owner);snapshot(ren,"selection");
        if(warm) {
            char path[1400];snprintf(path,sizeof(path),"%s/selection.sav",out);assert(load_state(path));
        }
        advance(2);
    } else if(stage==2 && menu_ready && presents-stage_at>12) {
        if(rotations) {click(hotspot(0,0,2));rotations--;stage_at=presents;}
        else if(action) {click(hotspot(0,0,3));advance(3);}
        else advance(4);
    } else if(stage==3 && menu_ready && presents-stage_at>15) {
        snapshot(ren,"after-item");advance(4);
    } else if(stage==4 && menu_ready && presents-stage_at>12) {
        snapshot(ren,"before-exit");
        if(close_key) {close_input(1);release_at=presents+3;}else click(hotspot(0,0,1));
        advance(5);
    } else if(stage==5 && presents-stage_at>110) {
        snapshot(ren,"after-exit");state("result");
        if(checkpoint)assert(checkpoint_saved);
        printf("RESULT slot=%d shortcut=%d item=%d warm=%d owner=%d keyboard=%d returned=%d mode=%u cursor=%u entries=%d cleanups=%d resume=%d checkpoint=%x\n",
            slot,close_key,action,warm,owner+1,keyboard_owner,returned,r32(0x2fb1c),r16(0x392c8),entries,exits,resume,checkpoint);
        SDL_Event e={0};e.type=SDL_QUIT;assert(SDL_PushEvent(&e)==1);advance(6);
    }
    if(presents>550){state("timeout");assert(!"scroll test stalled");}
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    const char *log=NULL;int n=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--scroll-out")&&i+1<argc)out=argv[++i];
        else if(!strcmp(argv[i],"--slot")&&i+1<argc)slot=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--owner")&&i+1<argc)owner=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--rotations")&&i+1<argc)rotations=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--pad"))keyboard_owner=0;
        else if(!strcmp(argv[i],"--shortcut"))close_key=1;
        else if(!strcmp(argv[i],"--item"))action=1;
        else if(!strcmp(argv[i],"--warm"))warm=1;
        else if(!strcmp(argv[i],"--resume"))resume=1;
        else if(!strcmp(argv[i],"--finish"))finish_only=1;
        else if(!strcmp(argv[i],"--checkpoint")&&i+1<argc)checkpoint=(unsigned)strtoul(argv[++i],NULL,16);
        else if(!strcmp(argv[i],"--checkpoint-reload"))checkpoint_reload=1;
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=argv[i+1];argv[n++]=argv[i];}
    }
    argv[n]=NULL;if(!log || !out){fputs("Explicit scratch --log and --scroll-out required\n",stderr);return 2;}
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");assert(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    for(int i=0;i<2;i++) {
        SDL_VirtualJoystickDesc d={0};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
        d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
        d.name="Moonstone scroll test controller";int ix=SDL_JoystickAttachVirtualEx(&d);assert(ix>=0);
        joy[i]=SDL_JoystickOpen(ix);assert(joy[i]);
    }
    return moonstone_main(n,argv);
}
