/* Test-only SDL campaign transitions with virtual pads. Never distribute. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
static Uint8 probe_keys[SDL_NUM_SCANCODES];
static const Uint8 *probe_keyboard(int *n) { if(n)*n=SDL_NUM_SCANCODES;return probe_keys; }
static SDL_bool probe_is_controller(int i) { return SDL_JoystickIsVirtual(i)&&SDL_IsGameController(i); }
static SDL_Window *probe_window(const char *t,int x,int y,int w,int h,Uint32 f) {
    return SDL_CreateWindow(t,x,y,w,h,f|SDL_WINDOW_HIDDEN);
}
static void probe_present(SDL_Renderer *ren);
#define SDL_GetKeyboardState probe_keyboard
#define SDL_IsGameController probe_is_controller
#define SDL_CreateWindow probe_window
#define SDL_RenderPresent probe_present
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef SDL_RenderPresent
#undef SDL_CreateWindow
#undef SDL_IsGameController
#undef SDL_GetKeyboardState
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL frame%d line%d: %s\n",frame,__LINE__,#x);exit(2);} }while(0)
static SDL_Joystick *pads[2];
static const char *out;
static int frame,mode_attack,prepare_attack,keyboard_p2,expect_old,stage,age,launch_attack,launched;
static int swap_start=-1,swap_end=-1,swap_owner,combat_seen,returned_math;
static void button(int p,SDL_GameControllerButton b,int down) {
    if(p==1 && keyboard_p2) {
        SDL_Scancode sc=b==SDL_CONTROLLER_BUTTON_START?SDL_SCANCODE_RETURN:
            b==SDL_CONTROLLER_BUTTON_A?SDL_SCANCODE_LCTRL:
            b==SDL_CONTROLLER_BUTTON_DPAD_UP?SDL_SCANCODE_UP:SDL_SCANCODE_UNKNOWN;
        CHECK(sc!=SDL_SCANCODE_UNKNOWN);
        if(probe_keys[sc]!=(Uint8)down) {
            SDL_Event e={0};probe_keys[sc]=(Uint8)down;
            e.type=down?SDL_KEYDOWN:SDL_KEYUP;e.key.keysym.scancode=sc;
            e.key.keysym.sym=SDL_GetKeyFromScancode(sc);CHECK(SDL_PushEvent(&e)==1);
        }
        return;
    }
    CHECK(SDL_JoystickSetVirtualButton(pads[p],b,(Uint8)down)==0);
}
static int p2_device(void) { return keyboard_p2?MP_KEYBOARD:SDL_JoystickInstanceID(pads[1]); }
void moon_instr_hook(unsigned pc) {
    int armed=g_autoswap_armed;
    production_hook(pc);
    if(!armed && g_autoswap_armed && swap_start<0) {
        swap_start=g_cur_frame;swap_owner=g_mp_ui_player;
        fprintf(g_log,"PROBE-SWAP start fr=%d owner=%d\n",swap_start,swap_owner);
    }
    if(armed && !g_autoswap_armed && swap_start>=0 && swap_end<0) {
        swap_end=g_cur_frame;
        fprintf(g_log,"PROBE-SWAP end fr=%d owner=%d\n",swap_end,g_mp_ui_player);
    }
    if(launch_attack && pc==0x40188 && r16(pc)==0x2079) {
        launch_attack=0;launched=1;
        uint32_t a=0x2e8e4,b=0x2e860,sp=m68k_get_reg(NULL,M68K_REG_SP)-4;
        w8(a+0x12,0);w8(b+0x12,0); /* fixture has no protection-scroll detour */
        w32(0x2ebd0,a);w16(0x2f9da,2);
        w32(sp,pc);m68k_set_reg(M68K_REG_SP,sp);
        m68k_set_reg(M68K_REG_A0,a);m68k_set_reg(M68K_REG_A1,b);
        m68k_set_reg(M68K_REG_PC,0x21ab4); /* original AI-versus-P2 encounter */
    }
    if(pc==0x22fc4 && launched && swap_end>=0) combat_seen=1;
    if(prepare_attack && stage==3 && !returned_math && pc==0x40188 && r16(pc)==0x2079) {
        char path[1200];snprintf(path,sizeof(path),"%s/map-after-math.sav",out);
        CHECK(g_mp_ui_player==1 && g_disk_inserted==2 && save_state(path));
        returned_math=1;
    }
}
static void snapshot(SDL_Renderer *ren,const char *name) {
    char path[1200];snprintf(path,sizeof(path),"%s/%s.sav",out,name);CHECK(save_state(path));
    snprintf(path,sizeof(path),"%s/%s.bmp",out,name);
    int w,h;CHECK(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(s&&SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    CHECK(SDL_SaveBMP(s,path)==0);SDL_FreeSurface(s);
}
static void quit(void) { SDL_Event e={.type=SDL_QUIT};CHECK(SDL_PushEvent(&e)==1);stage=4; }
static void probe_present(SDL_Renderer *ren) {
    frame++;
    if(frame==1) {
        CHECK(g_mp_campaign&&g_mp.players==2);
        fprintf(g_log,"PROBE-INITIAL disk=%d pc=%x\n",g_disk_inserted,m68k_get_reg(NULL,M68K_REG_PC));
        /* Represent P1's earlier identification; P2 identifies through real SDL.
         * This keeps P1 entirely idle during the transition under test. */
        g_mp.device[0]=SDL_JoystickInstanceID(pads[0]);g_mp.enrolled|=1;
    }
    if(stage==0 && g_mp.phase!=MP_PLAY) {
        CHECK(g_mp.claim==1);button(1,SDL_CONTROLLER_BUTTON_START,1);stage=1;age=0;
    } else if(stage==1) {
        if(++age==3)button(1,SDL_CONTROLLER_BUTTON_START,0);
        if(age>3 && g_mp.phase==MP_PLAY && g_mp_context==MP_CAM_MAP && g_mp_ui_player==1) {
            CHECK(g_mp.device[1]==p2_device());
            /* Keep the fixture's native drive/cache state intact. */
            if(mode_attack)launch_attack=1;
            else button(1,SDL_CONTROLLER_BUTTON_A,1);
            stage=2;age=0;
        }
    } else if(stage==2) {
        age++;
        if(age==8)button(1,SDL_CONTROLLER_BUTTON_A,0);
        if(!mode_attack && swap_start<0 && age>10) {
            /* Math is the only fixed node at this fixture's coordinates. */
            button(1,SDL_CONTROLLER_BUTTON_DPAD_UP,age%20<4);
        }
        if(swap_start>=0 && swap_end<0 && g_cur_frame-swap_start>=80 && expect_old) {
            CHECK(g_autoswap_armed);snapshot(ren,"blocked");
            printf("REPRO: %s remains in hidden disk wait with idle controllers; owner=P%d\n",
                   mode_attack?"AI attacks P2":"P2 enters Math",g_mp_ui_player+1);
            quit();
        } else if(swap_end>=0) {
            CHECK(!expect_old && swap_end-swap_start<=13);
            CHECK(g_mp.device[0]==SDL_JoystickInstanceID(pads[0])&&g_mp.device[1]==p2_device());
            stage=3;age=0;
        }
    } else if(stage==3) {
        age++;
        if(prepare_attack && age>500) {
            if(g_mp_context==MP_CAM_UI && age>800) {
                /* Put the native inventory cursor over Exit; use P2's real
                 * fire input and the original menu action to leave Math. */
                w16(0x392d4,83);w16(0x392d6,96);
            }
            button(1,SDL_CONTROLLER_BUTTON_A,age%40<6);
            if(returned_math) {
                button(1,SDL_CONTROLLER_BUTTON_A,0);
                CHECK(g_disk_inserted==2);
                snapshot(ren,"map-after-math-view");
                printf("PASS: native return from Math; disk C; map owner P%d\n",g_mp_ui_player+1);
                quit();
            }
        } else if((mode_attack&&combat_seen&&age>30)||(!prepare_attack&&!mode_attack&&age>500)) {
            CHECK(g_mp.phase==MP_PLAY&&g_mp_ui_player==1);
            snapshot(ren,"arrived");
            printf("PASS: %s; automatic swap %d frames; P2 retains %s, P1 never pressed\n",
                   mode_attack?"AI attacks P2":"P2 enters Math",swap_end-swap_start,
                   keyboard_p2?"keyboard":"controller");
            quit();
        }
    }
    CHECK(frame<(prepare_attack?4000:1500));
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--log")&&i+1<argc){log=1;i++;}
        else if(!strcmp(argv[i],"--probe-output")&&i+1<argc)out=argv[++i];
        else if(!strcmp(argv[i],"--probe-attack"))mode_attack=1;
        else if(!strcmp(argv[i],"--prepare-attack"))prepare_attack=1;
        else if(!strcmp(argv[i],"--keyboard-p2"))keyboard_p2=1;
        else if(!strcmp(argv[i],"--expect-old"))expect_old=1;
    }
    if(!log||!out)return 2;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    for(int p=0;p<2;p++) {
        SDL_VirtualJoystickDesc d={0};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;
        d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
        d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;d.name="Moonstone transition test";
        int i=SDL_JoystickAttachVirtualEx(&d);CHECK(i>=0);pads[p]=SDL_JoystickOpen(i);CHECK(pads[p]);
    }
    int rc=moonstone_main(argc,argv);CHECK(stage==4);return rc;
}
