/* Actual Windows/SDL window and input-loop regression. Never ship this probe. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static SDL_Window *window;
static SDL_Joystick *pads[2];
static int presents, fail_next, switches;
static const char *scenario, *output;
static const Uint8 *probe_keyboard(int *n) { if (n) *n=SDL_NUM_SCANCODES; return keys; }
static SDL_bool virtual_controller(int i) { return SDL_JoystickIsVirtual(i) && SDL_IsGameController(i); }
static SDL_Window *probe_window(const char *t,int x,int y,int w,int h,Uint32 flags) {
    window=SDL_CreateWindow(t,x,y,w,h,flags);
    return window;
}
static int probe_fullscreen(SDL_Window *w,Uint32 flags) {
    switches++;
    if (fail_next) { fail_next=0; return SDL_SetError("Injected fullscreen failure"); }
    return SDL_SetWindowFullscreen(w,flags);
}
static void present(SDL_Renderer *ren);
#define SDL_GetKeyboardState probe_keyboard
#define SDL_IsGameController virtual_controller
#define SDL_CreateWindow probe_window
#define SDL_SetWindowFullscreen probe_fullscreen
#define SDL_RenderPresent present
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef SDL_RenderPresent
#undef SDL_SetWindowFullscreen
#undef SDL_CreateWindow
#undef SDL_IsGameController
#undef SDL_GetKeyboardState
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s frame %d line %d: %s (SDL: %s)\n",scenario,presents,__LINE__,#x,SDL_GetError()); exit(2); } } while (0)
static int is(const char *s) { return !strcmp(scenario,s); }
static int fullscreen(void) { return !!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN); }
static SDL_Rect rect(void) {
    SDL_Rect r; SDL_GetWindowPosition(window,&r.x,&r.y); SDL_GetWindowSize(window,&r.w,&r.h); return r;
}
static void key(SDL_Scancode sc,int down,int repeat) {
    keys[sc]=(Uint8)down;
    SDL_Event e={0}; e.type=down?SDL_KEYDOWN:SDL_KEYUP;
    e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.scancode=sc; e.key.keysym.sym=SDL_GetKeyFromScancode(sc);
    e.key.repeat=(Uint8)repeat; CHECK(SDL_PushEvent(&e)==1);
}
static void focus(int gained) {
    /* Automated foreground activation is OS-restricted. Exercise SDL's actual
     * focus-event path deterministically, alongside real minimize/restore. */
    SDL_Event e={0}; e.type=SDL_WINDOWEVENT; e.window.windowID=SDL_GetWindowID(window);
    e.window.event=gained?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;
    CHECK(SDL_PushEvent(&e)==1);
}
static void picture(SDL_Renderer *ren,const char *name) {
    char path[1200]; snprintf(path,sizeof(path),"%s/%s.bmp",output,name);
    int w,h; CHECK(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    /* Read the entire output, including both letterbox bars. SDL otherwise
     * reads relative to the logical viewport into the start of the buffer. */
    int lw,lh; SDL_RenderGetLogicalSize(ren,&lw,&lh);
    CHECK(SDL_RenderSetLogicalSize(ren,0,0)==0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(s && SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    CHECK(SDL_SaveBMP(s,path)==0); SDL_FreeSurface(s);
    CHECK(SDL_RenderSetLogicalSize(ren,lw,lh)==0);
}
static void check_scaling(SDL_Renderer *ren) {
    int w,h,lw,lh; float x,y; SDL_Rect viewport;
    CHECK(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    SDL_RenderGetLogicalSize(ren,&lw,&lh); SDL_RenderGetScale(ren,&x,&y);
    SDL_RenderGetViewport(ren,&viewport);
    printf("SCALING frame=%d output=%dx%d logical=%dx%d scale=%.4f,%.4f viewport=%d,%d,%d,%d\n",
           presents,w,h,lw,lh,x,y,viewport.x,viewport.y,viewport.w,viewport.h);
    CHECK(lw==320 && lh==256 && fabsf(x-y)<0.001f);
    CHECK(fabsf(fminf((float)w/320,(float)h/256)-x)<0.01f);
    /* SDL reports an integer logical viewport; fractional scale can round down. */
    CHECK(viewport.w>=319 && viewport.w<=320 && viewport.h>=255 && viewport.h<=256);
}
static SDL_Joystick *attach_pad(void) {
    SDL_VirtualJoystickDesc d={0}; d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER; d.naxes=SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons=SDL_CONTROLLER_BUTTON_MAX; d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
    d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1; d.name="Moonstone fullscreen test";
    int index=SDL_JoystickAttachVirtualEx(&d); CHECK(index>=0);
    SDL_Joystick *pad=SDL_JoystickOpen(index); CHECK(pad); return pad;
}
static void detach_pad(SDL_Joystick **pad) {
    SDL_JoystickID id=SDL_JoystickInstanceID(*pad);
    for (int i=0;i<SDL_NumJoysticks();i++) if (SDL_JoystickGetDeviceInstanceID(i)==id) {
        CHECK(SDL_JoystickDetachVirtual(i)==0); break;
    }
    SDL_JoystickClose(*pad); *pad=NULL;
}
static void start(int down) {
    CHECK(SDL_JoystickSetVirtualButton(pads[0],SDL_CONTROLLER_BUTTON_START,(Uint8)down)==0);
}
static void present(SDL_Renderer *ren) {
    static SDL_Rect original, normal;
    static MpSession session;
    static int frozen_frame, focus_frame;
    int n=++presents, control=is("control"), prompt=is("prompt"), recovery=is("reconnect");
    CHECK(n<=81);
    if (n==1) {
        CHECK(!fullscreen());
        SDL_SetWindowSize(window,640,512); SDL_SetWindowPosition(window,80,90);
    }
    if (n==2 && (recovery || is("focus"))) start(1);
    if (n==3 && (recovery || is("focus"))) start(0);
    if (n==4 && is("maximized")) { normal=rect(); SDL_MaximizeWindow(window); }
    if (n==5 && recovery) { CHECK(g_mp.phase==MP_PLAY); detach_pad(&pads[1]); }
    if (n==5 && is("failure")) { fail_next=1; key(SDL_SCANCODE_F11,1,0); }
    if (n==6 && is("failure")) { CHECK(!fullscreen()); key(SDL_SCANCODE_F11,0,0); }
    if (n==8) {
        original=rect(); session=g_mp; frozen_frame=g_cur_frame;
        if (is("maximized")) CHECK(SDL_GetWindowFlags(window)&SDL_WINDOW_MAXIMIZED);
        if (prompt || recovery) CHECK(mp_active() && g_mp.phase==MP_CLAIM);
        if (recovery) CHECK(g_mp.claim==1 && g_mp.recovery);
    }
    if (n==10 && !control) key(SDL_SCANCODE_F11,1,0);
    if (n==12) {
        CHECK(fullscreen()==!control); check_scaling(ren); picture(ren,"fullscreen");
        if (fullscreen()) {
            SDL_Rect display; CHECK(SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(window),&display)==0);
            SDL_Rect r=rect(); CHECK(!memcmp(&r,&display,sizeof(r)));
        }
    }
    if (n==14 && !control) key(SDL_SCANCODE_F11,1,1);
    if (n==16 && !control) { CHECK(fullscreen()); key(SDL_SCANCODE_F11,0,0); }
    if (n==18 && is("focus")) {
        CHECK(g_mp.phase==MP_PLAY); focus_frame=g_cur_frame;
        SDL_MinimizeWindow(window); focus(0);
    }
    if (n==21 && is("focus")) {
        CHECK(g_cur_frame==focus_frame);
        SDL_RestoreWindow(window); SDL_RaiseWindow(window); focus(1);
    }
    if (n==24 && is("focus")) {
        CHECK(fullscreen() && !(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED));
        CHECK(g_cur_frame>focus_frame && g_mp.phase==MP_PLAY);
        CHECK(!memcmp(g_mp.device,session.device,sizeof(session.device)));
        check_scaling(ren);
    }
    if (n==30 && !control) { if (is("failure")) fail_next=1; key(SDL_SCANCODE_ESCAPE,1,0); }
    if (n==32) {
        CHECK(fullscreen()==is("failure"));
        if (is("remap")) CHECK(!g_fire && !g_fire2 && !g_ji_up);
    }
    if (n==34 && !control) key(SDL_SCANCODE_ESCAPE,1,1);
    if (n==38 && is("remap")) CHECK(!g_fire && !g_fire2 && !g_ji_up);
    if (n==40 && !control) key(SDL_SCANCODE_ESCAPE,0,0);
    if (n==42 && is("failure")) key(SDL_SCANCODE_ESCAPE,1,0);
    if (n==44 && is("failure")) key(SDL_SCANCODE_ESCAPE,0,0);
    if (n==48) {
        CHECK(!fullscreen()); SDL_Rect r=rect(); CHECK(!memcmp(&r,&original,sizeof(r)));
        if (is("maximized")) CHECK(SDL_GetWindowFlags(window)&SDL_WINDOW_MAXIMIZED);
        check_scaling(ren); picture(ren,"windowed");
    }
    if (n==50 && !control) key(SDL_SCANCODE_F11,1,0);
    if (n==52 && !control) { CHECK(fullscreen()); key(SDL_SCANCODE_F11,0,0); }
    if (n==60 && !control) key(SDL_SCANCODE_F11,1,0);
    if (n==62) { CHECK(!fullscreen()); if (!control) key(SDL_SCANCODE_F11,0,0); }
    if (n==65 && is("maximized")) SDL_RestoreWindow(window);
    if (n==68 && is("maximized")) {
        SDL_Rect r=rect(); CHECK(!(SDL_GetWindowFlags(window)&SDL_WINDOW_MAXIMIZED));
        CHECK(!memcmp(&r,&normal,sizeof(r)));
    }
    if (n==68 && (prompt || recovery)) {
        CHECK(g_cur_frame==frozen_frame && !memcmp(&g_mp,&session,sizeof(session)));
        if (recovery) { pads[1]=attach_pad(); }
    }
    if (n==70 && recovery) CHECK(SDL_JoystickSetVirtualButton(pads[1],SDL_CONTROLLER_BUTTON_START,1)==0);
    if (n==72 && recovery) CHECK(SDL_JoystickSetVirtualButton(pads[1],SDL_CONTROLLER_BUTTON_START,0)==0);
    if (n==76 && recovery) {
        CHECK(g_mp.phase==MP_PLAY && g_mp.device[0]==session.device[0]);
        CHECK(g_mp.device[1]==SDL_JoystickInstanceID(pads[1]));
        CHECK(g_cur_frame>frozen_frame && !g_pause_request);
    }
    if (n==78) {
        char path[1200]; snprintf(path,sizeof(path),"%s/final.sav",output); CHECK(save_state(path));
        CHECK(switches==(control?0:is("failure")?6:4));
        if (is("reserved")) CHECK(!control_key_matches(CONTROL_FIRE,SDL_SCANCODE_F11));
    }
    if (n==80) key(is("remap")?SDL_SCANCODE_F8:SDL_SCANCODE_ESCAPE,1,0);
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int log=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--log") && i+1<argc) { log=1; i++; }
        else if (!strcmp(argv[i],"--probe-output") && i+1<argc) output=argv[++i];
        else if (!strcmp(argv[i],"--probe-case") && i+1<argc) scenario=argv[++i];
    }
    if (!log || !output || !scenario) return 2;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if (is("prompt") || is("reconnect") || is("focus")) {
        SDL_SetMainReady(); CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
        pads[0]=attach_pad(); pads[1]=attach_pad();
    }
    int rc=moonstone_main(argc,argv);
    CHECK(presents==81 && rc==0);
    printf("PASS %s: real SDL fullscreen, Escape/F11, held input and window restoration\n",scenario);
    return rc;
}
