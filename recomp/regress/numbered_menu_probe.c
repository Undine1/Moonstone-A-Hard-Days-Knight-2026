/* Test-only native parser and actual SDL input checks. Never distribute.
 * Uses copied saves, virtual controllers and an explicit scratch --log. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static int presents,dispatches,native,oracle,choice,owner=2,keyboard_mode,expected=3,recovery,action_seen;
static uint32_t wait_pc=0x41178,selected_record,selected_kind;
static const char *output,*fixture;
static SDL_Joystick *pads[2];
static SDL_JoystickID ids[2];
static const Uint8 *test_keyboard(int *n) { if(n)*n=SDL_NUM_SCANCODES;return keys; }
static SDL_bool virtual_controller(int i) { return SDL_JoystickIsVirtual(i)&&SDL_IsGameController(i); }
static SDL_Window *hidden_window(const char *t,int x,int y,int w,int h,Uint32 f) {
    return SDL_CreateWindow(t,x,y,w,h,f|SDL_WINDOW_HIDDEN);
}
static void present(SDL_Renderer *ren);
#define SDL_GetKeyboardState test_keyboard
#define SDL_IsGameController virtual_controller
#define SDL_CreateWindow hidden_window
#define SDL_RenderPresent present
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef SDL_RenderPresent
#undef SDL_CreateWindow
#undef SDL_IsGameController
#undef SDL_GetKeyboardState
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL present %d line %d: %s\n",presents,__LINE__,#x);exit(2);} } while(0)

void moon_instr_hook(unsigned pc) {
    if(!oracle) production_hook(pc);
    if(pc==0x21ca4||pc==0x21ab4)action_seen=1;
    if(pc==wait_pc+0x62) {
        uint32_t item=m68k_get_reg(NULL,M68K_REG_A2)-8;
        choice=(int)((item-r32(wait_pc+36))/8)+1;
        selected_record=r32(item);selected_kind=r32(item+4);dispatches++;
        if(native) m68k_end_timeslice();
    }
}
static void button(int pad,SDL_GameControllerButton b,int down) {
    CHECK(SDL_JoystickSetVirtualButton(pads[pad],b,(Uint8)down)==0);
}
static void key(SDL_Scancode sc,int down) {
    keys[sc]=(Uint8)down;SDL_Event e={0};e.type=down?SDL_KEYDOWN:SDL_KEYUP;
    e.key.keysym.scancode=sc;e.key.keysym.sym=SDL_GetKeyFromScancode(sc);
    CHECK(SDL_PushEvent(&e)==1);
}
static void picture(SDL_Renderer *ren,const char *name) {
    char path[1400];snprintf(path,sizeof(path),"%s/%s.bmp",output,name);
    int w,h;CHECK(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(s&&SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    CHECK(SDL_SaveBMP(s,path)==0);SDL_FreeSurface(s);
}
static void check_selected(int n) {
#ifndef NUMBERED_MENU_BEFORE
    CHECK(numbered_menu_live()&&g_numbered.selected==n&&g_numbered.count==3);
#else
    (void)n;
#endif
    CHECK(dispatches==0);
}
static void setup_session(void) {
    CHECK(g_mp_campaign&&g_mp_ui_player==owner&&g_mp.required==(1u<<owner));
    int players=mp_campaign_players();
    mp_begin_campaign(&g_mp,players,1,1);
    for(int p=0;p<players;p++) {
        g_mp.required=1u<<p;mp_next(&g_mp);
        CHECK(mp_claim(&g_mp,p==owner?(keyboard_mode?MP_KEYBOARD:ids[1]):ids[0]));
        g_mp.phase=MP_PLAY;
    }
    g_mp.required=1u<<owner;mp_use_choices(&g_mp,owner,0);
    CHECK(g_mp.phase==MP_PLAY);
}
static SDL_Joystick *attach_pad(void) {
    SDL_VirtualJoystickDesc d={0};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
    d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;d.name="Moonstone numbered menu test";
    int index=SDL_JoystickAttachVirtualEx(&d);CHECK(index>=0);
    SDL_Joystick *pad=SDL_JoystickOpen(index);CHECK(pad);return pad;
}
static void focus(SDL_Renderer *ren,int on) {
    SDL_Event e={.type=SDL_WINDOWEVENT};e.window.windowID=SDL_GetWindowID(SDL_RenderGetWindow(ren));
    e.window.event=on?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;
    CHECK(SDL_PushEvent(&e)==1);
}
static void present(SDL_Renderer *ren) {
    int n=++presents;
    if(recovery&&n>=31&&n<=60) {
        if(n==31)focus(ren,0);
        if(n==33){check_selected(3);focus(ren,1);}
        if(n==35)check_selected(3);
        if(n==36) {
            int found=0;
            for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_JoystickGetDeviceInstanceID(i)==ids[1]) {
                CHECK(SDL_JoystickDetachVirtual(i)==0);found=1;break;
            }
            CHECK(found);SDL_JoystickClose(pads[1]);pads[1]=NULL;
        }
        if(n==38){CHECK(g_mp.phase==MP_CLAIM&&g_mp.claim==owner);button(0,SDL_CONTROLLER_BUTTON_START,1);}
        if(n==40){CHECK(g_mp.phase==MP_CLAIM&&g_mp.claim==owner);button(0,SDL_CONTROLLER_BUTTON_START,0);}
        if(n==43){pads[1]=attach_pad();ids[1]=SDL_JoystickInstanceID(pads[1]);}
        if(n==45){CHECK(g_mp.phase==MP_CLAIM);button(1,SDL_CONTROLLER_BUTTON_START,1);}
        if(n==47)button(1,SDL_CONTROLLER_BUTTON_START,0);
        if(n==49){CHECK(g_mp.phase==MP_PLAY&&g_mp.device[owner]==ids[1]);check_selected(3);picture(ren,"recovered");}
        SDL_RenderPresent(ren);return;
    }
    if(recovery&&n>60)n-=30;
    if(n==1)setup_session();
    if(n==4){check_selected(1);picture(ren,"initial");}
    /* Neither another player's pad nor their (or an unassigned) keyboard may
     * move/confirm a controller-owned menu. */
    if(n==5){button(0,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);button(0,SDL_CONTROLLER_BUTTON_A,1);}
    if(n==7){check_selected(1);button(0,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);button(0,SDL_CONTROLLER_BUTTON_A,0);}
    if(n==9&&!keyboard_mode)key(SDL_SCANCODE_3,1);
    if(n==10&&!keyboard_mode){check_selected(1);key(SDL_SCANCODE_3,0);}
    if(n==12) {
        if(keyboard_mode)key(SDL_SCANCODE_9,1); /* invalid index stays in the menu */
        else button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
    }
    if(n==14) {
        check_selected(keyboard_mode?1:2);
        if(keyboard_mode)key(SDL_SCANCODE_9,0);
    }
    if(n==17) { /* Holding Down never repeats through the entire list. */
        check_selected(keyboard_mode?1:2);button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);
    }
    if(n==19&&!keyboard_mode)button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
    if(n==21) {
        check_selected(keyboard_mode?1:3);picture(ren,"third");
        button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);
    }
    if(n==23&&!keyboard_mode){button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);}
    if(n==25) {check_selected(1);button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);}
    if(n==27&&!keyboard_mode)button(1,SDL_CONTROLLER_BUTTON_DPAD_UP,1);
    if(n==29) {check_selected(keyboard_mode?1:3);button(1,SDL_CONTROLLER_BUTTON_DPAD_UP,0);}
    if(n==31) {
        /* Warm F9 rebuilds the menu cursor and keeps the correct active owner. */
        CHECK(load_state(fixture));CHECK(g_mp_ui_player==owner&&g_mp.phase==MP_PLAY);
    }
    if(n==34)check_selected(1);
    if(n==35&&!keyboard_mode&&expected>=2)button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
    if(n==37)button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);
    if(n==39&&!keyboard_mode&&expected==3)button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
    if(n==41){check_selected(keyboard_mode?1:expected);button(1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);}
    if(n==43) {
        if(keyboard_mode)key((SDL_Scancode)((keyboard_mode==2?SDL_SCANCODE_KP_1:SDL_SCANCODE_1)+expected-1),1);
        else button(1,SDL_CONTROLLER_BUTTON_A,1);
    }
    if(n==45) {
        CHECK(dispatches==1&&choice==expected&&action_seen);
        CHECK(selected_kind==(expected==3?2:1));
#ifndef NUMBERED_MENU_BEFORE
        CHECK(!numbered_menu_live());
#endif
        picture(ren,"dispatched");
        SDL_Event e={.type=SDL_QUIT};CHECK(SDL_PushEvent(&e)==1);
        printf("PASS SDL P%d %s option %d: ownership, held directions, wrap, warm load, native dispatch\n",
               owner+1,keyboard_mode==2?"keypad":keyboard_mode?"keyboard":"controller",expected);
    }
    CHECK(n<=46);SDL_RenderPresent(ren);
}

#ifndef NUMBERED_MENU_BEFORE
static uint8_t image[RAM_SIZE],oracle_ram[RAM_SIZE];
static void native_reset(int count) {
    memcpy(g_ram,image,RAM_SIZE);g_os=1;g_sdl_mode=0;g_stop=0;
    g_ji_up=g_ji_dn=g_fire=g_kdigit=0;g_popup_injected=0;g_cur_frame++;
    numbered_menu_reset();mp_campaign_clear();mp_reset(&g_mp);
    uint32_t list=r32(wait_pc+36);
    for(int i=0;i<count;i++){w32(list+8*i,0x10000+0x100*i);w32(list+8*i+4,i+1);}
    w32(list+8*count,0);w16(r32(wait_pc+2),0);
    m68k_set_reg(M68K_REG_SR,0x2700);
    for(int r=0;r<15;r++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+r),0x10101010u+(unsigned)r);
    m68k_set_reg(M68K_REG_A7,0x1ff000);
    m68k_set_reg(M68K_REG_PC,wait_pc);dispatches=0;choice=0;
}
static void tick(void) { g_cur_frame++;m68k_execute(500); }
static void matrix(const char *retail) {
    FILE *f=fopen(fixture,"rb");CHECK(f);CHECK(fseek(f,104,SEEK_SET)==0);
    CHECK(fread(image,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);native=1;
    unsigned cases=0;
    for(int edition=0;edition<2;edition++) {
        if(edition){f=fopen(retail,"rb");CHECK(f);CHECK(fseek(f,104,SEEK_SET)==0);CHECK(fread(image,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);}
        g_lineage=edition?LIN_RETAIL:LIN_CRACKED;
        for(int relocated=0;relocated<2;relocated++) {
            wait_pc=edition?(relocated?0x1e43aa:0x40d96):(relocated?0x1e476c:0x41178);
            for(int count=2;count<=9;count++)for(int target=1;target<=count;target++) {
                native_reset(count);CHECK(numbered_menu_wait_for_pc(wait_pc)==wait_pc);
                /* Independent oracle: original keyboard parser, with every host hook off. */
                oracle=1;w16(r32(wait_pc+2),(uint16_t)(target+1));tick();
                if(dispatches!=1||choice!=target)fprintf(stderr,"oracle wait=%x count=%d target=%d dispatch=%d choice=%d pc=%x raw=%x\n",wait_pc,count,target,dispatches,choice,m68k_get_reg(NULL,M68K_REG_PC),r16(r32(wait_pc+2)));
                CHECK(dispatches==1&&choice==target);
                uint32_t want_record=selected_record,want_kind=selected_kind;
                memcpy(oracle_ram,g_ram,RAM_SIZE);oracle=0;
                native_reset(count);
                g_fire=1;numbered_menu_hook(wait_pc-0x16);tick();
                CHECK(dispatches==0&&g_numbered.selected==1);g_fire=0;tick();
                for(int j=1;j<target;j++) {
                    g_ji_dn=1;tick();tick();CHECK(!dispatches&&g_numbered.selected==j+1);
                    g_ji_dn=0;tick();
                }
                g_fire=1;tick();CHECK(dispatches==1&&choice==target);
                CHECK(selected_record==want_record&&selected_kind==want_kind);
                CHECK(!memcmp(g_ram,oracle_ram,RAM_SIZE));CHECK(!numbered_menu_live());cases++;
                /* A save at the poll has no host cursor: held input must not pick
                 * anything until a new press. Invalid digits are also ignored. */
                native_reset(count);g_ji_up=g_fire=1;g_kdigit=9;tick();
                CHECK(!dispatches&&g_numbered.selected==1);
                g_ji_up=g_fire=g_kdigit=0;tick();
                if(count<9){g_kdigit=9;tick();CHECK(!dispatches);g_kdigit=0;tick();}
                g_kdigit=target;tick();CHECK(dispatches==1&&choice==target);cases++;
            }
            native_reset(3);w16(wait_pc,0x4e71);
            CHECK(!numbered_menu_wait_for_pc(wait_pc));numbered_menu_hook(wait_pc);CHECK(!g_numbered.wait);
        }
    }
    printf("PASS %u native parser cases: both disk lineages/copies, 2..9 choices, unchanged native RAM/results, held and invalid input, opcode guards\n",cases);
}
#endif
int main(int argc,char **argv) {
    const char *log=NULL,*retail=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--log")&&i+1<argc)log=argv[++i];
        else if(!strcmp(argv[i],"--probe-output")&&i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--loadstate")&&i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--probe-keyboard")&&i+1<argc)keyboard_mode=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--probe-choice")&&i+1<argc)expected=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--probe-owner")&&i+1<argc)owner=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--probe-retail")&&i+1<argc)retail=argv[++i];
        else if(!strcmp(argv[i],"--probe-recovery"))recovery=1;
    }
    CHECK(log&&fixture);
#ifndef NUMBERED_MENU_BEFORE
    if(retail){g_log=fopen(log,"w");CHECK(g_log);matrix(retail);fclose(g_log);return 0;}
#endif
    CHECK(output&&owner>=0&&owner<4&&expected>=1&&expected<=3);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    for(int i=0;i<2;i++) {
        pads[i]=attach_pad();ids[i]=SDL_JoystickInstanceID(pads[i]);
    }
    int rc=moonstone_main(argc,argv);CHECK(presents==(recovery?76:46));return rc;
}
