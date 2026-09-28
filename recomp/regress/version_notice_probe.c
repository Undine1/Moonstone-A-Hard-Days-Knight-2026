/* Test-only real SDL input/render-loop checks. Never distribute this executable. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static int presents, map_polls, old_waits, control, controller, remap;
static const char *output;
static SDL_Joystick *pad;
static const Uint8 *version_keyboard(int *n) { if (n) *n=SDL_NUM_SCANCODES; return keys; }
static SDL_bool virtual_controller(int i) { return SDL_JoystickIsVirtual(i) && SDL_IsGameController(i); }
static SDL_Window *hidden_window(const char *t,int x,int y,int w,int h,Uint32 f) {
    return SDL_CreateWindow(t,x,y,w,h,f|SDL_WINDOW_HIDDEN);
}
static void present(SDL_Renderer *ren);
#define SDL_GetKeyboardState version_keyboard
#define SDL_IsGameController virtual_controller
#define SDL_CreateWindow hidden_window
#define SDL_RenderPresent present
#define moon_instr_hook original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef SDL_RenderPresent
#undef SDL_CreateWindow
#undef SDL_IsGameController
#undef SDL_GetKeyboardState
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL frame %d line %d: %s\n",presents,__LINE__,#x); exit(2); } } while (0)
void moon_instr_hook(unsigned pc) {
    if (pc==0x4024c) map_polls++;
    if (pc==0x3b930 && r32(m68k_get_reg(NULL,M68K_REG_A7))==0x3bf4e
        && r32(m68k_get_reg(NULL,M68K_REG_A7)+4)==0x402a2) old_waits++;
    original_hook(pc);
}
static void key(SDL_Scancode sc, int down, int repeat) {
    keys[sc]=(Uint8)down;
    SDL_Event e={0}; e.type=down?SDL_KEYDOWN:SDL_KEYUP;
    e.key.keysym.scancode=sc; e.key.keysym.sym=SDL_GetKeyFromScancode(sc);
    e.key.repeat=(Uint8)repeat; CHECK(SDL_PushEvent(&e)==1);
}
static void version(int down,int repeat) {
    if (control) return;
    if (controller) CHECK(SDL_JoystickSetVirtualButton(pad,SDL_CONTROLLER_BUTTON_X,(Uint8)down)==0);
    else key(remap?SDL_SCANCODE_B:SDL_SCANCODE_V,down,repeat);
}
static void picture(SDL_Renderer *ren,const char *name) {
    char path[1200]; snprintf(path,sizeof(path),"%s/%s.bmp",output,name);
    int w,h; CHECK(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(s && SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    CHECK(SDL_SaveBMP(s,path)==0); SDL_FreeSurface(s);
}
static void present(SDL_Renderer *ren) {
    int n=++presents;
    if (n==1) {
        CHECK(g_map_live && !g_in_inventory && !mp_active());
        if (controller) controller_set_primary(SDL_JoystickInstanceID(pad),"version test");
    }
    if (n==3 && remap) key(SDL_SCANCODE_V,1,0);
    if (n==4 && remap) { CHECK(!g_version_visible); key(SDL_SCANCODE_V,0,0); }
    if (n==5) version(1,0);
    if (n==6) { CHECK(g_version_visible==!control); picture(ren,"shown"); }
    if (n==8) key(SDL_SCANCODE_RIGHT,1,0);
    if (n==10) version(1,1); /* OS repeat / still-held controller must not toggle. */
    if (n==18) {
        fprintf(stdout,"movement=%d map_polls=%d version_waits=%d\n",g_ji_rt,map_polls,old_waits);
        CHECK(g_ji_rt && map_polls>5 && !old_waits); key(SDL_SCANCODE_RIGHT,0,0);
    }
    if (n==240) { CHECK(g_version_visible==!control); version(0,0); }
    if (n==242) version(1,0);
    if (n==244) { CHECK(!g_version_visible); picture(ren,"hidden"); version(0,0); }
    if (n==250) version(1,0);
    if (n==252) { CHECK(g_version_visible==!control); version(0,0); key(SDL_SCANCODE_F5,1,0); }
    if (n==254) key(SDL_SCANCODE_F5,0,0);
    if (n==256) key(SDL_SCANCODE_F9,1,0);
    if (n==258) { CHECK(!g_version_visible); key(SDL_SCANCODE_F9,0,0); }
    if (n==260) version(1,0);
    if (n==262) { CHECK(g_version_visible==!control); version(0,0); key(SDL_SCANCODE_SPACE,1,0); }
    if (n==264) key(SDL_SCANCODE_SPACE,0,0);
    if (n==275) { CHECK(g_in_inventory && !g_version_visible); version(1,0); }
    if (n==278) { CHECK(!g_version_visible); version(0,0); }
    if (n==280) {
        CHECK(!old_waits && map_polls>100);
        char path[1200]; snprintf(path,sizeof(path),"%s/final.sav",output);
        CHECK(save_state(path)); picture(ren,"inventory");
        SDL_Event e={.type=SDL_QUIT}; CHECK(SDL_PushEvent(&e)==1);
        puts("PASS: toggle, held/repeated input, live movement, remapping, save/load and inventory scope");
    }
    CHECK(n<=281);
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int log=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--log") && i+1<argc) { log=1; i++; }
        else if (!strcmp(argv[i],"--probe-output") && i+1<argc) output=argv[++i];
        else if (!strcmp(argv[i],"--probe-control")) control=1;
        else if (!strcmp(argv[i],"--probe-controller")) controller=1;
        else if (!strcmp(argv[i],"--probe-remap")) remap=1;
    }
    if (!log || !output) return 2;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if (controller) {
        SDL_SetMainReady(); CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
        SDL_VirtualJoystickDesc d={0}; d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER; d.naxes=SDL_CONTROLLER_AXIS_MAX;
        d.nbuttons=SDL_CONTROLLER_BUTTON_MAX; d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
        d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1; d.name="Moonstone version test";
        int index=SDL_JoystickAttachVirtualEx(&d); CHECK(index>=0);
        pad=SDL_JoystickOpen(index); CHECK(pad);
    }
    int rc=moonstone_main(argc,argv); CHECK(presents==281); return rc;
}
