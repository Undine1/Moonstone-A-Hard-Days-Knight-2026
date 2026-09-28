/* Test-only retail map-input oracle and real SDL End Turn/inventory checks.
 * Never distribute. Requires copied saves and an explicit scratch --log. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static int native,oracle,done,polled,inventory,advances,presents,keyboard_mode,warm;
static int owner=2,action,original_index;
static const char *fixture,*retail;
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
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL frame %d line %d: %s\n",presents,__LINE__,#x);exit(2);} } while(0)

void moon_instr_hook(unsigned pc) {
    if(!oracle)production_hook(pc);
    if(pc==mp_addr(0x4024c,0x3ff1e))polled++;
    if(pc==mp_addr(0x405b4,0x401ec))inventory++;
    if(pc==mp_addr(0x40334,0x40058)) {
        if(!advances)original_index=r16(mp_addr(0x2f9da,0x2f79e));
        advances++;
    }
    if(native&&(pc==mp_addr(0x402a2,0x3ffb2)||pc==mp_addr(0x405b4,0x401ec))) {
        done=1;m68k_end_timeslice();
    }
}
static uint8_t images[2][RAM_SIZE];
static void read_ram(const char *path,uint8_t *ram,int offset) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(fseek(f,offset,SEEK_SET)==0);
    CHECK(fread(ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
}
typedef struct { int poll,inventory,timer,counter; } Result;
static Result native_case(int edition,int player,int dead,int flags,int direction,int key,int parity) {
    memcpy(g_ram,images[edition],RAM_SIZE);
    g_lineage=edition?LIN_RETAIL:LIN_CRACKED;g_os=1;g_sdl_mode=0;
    g_retail_parity=parity;oracle=edition;g_stop=0;g_cur_frame=100;
    g_rest_request=g_rest_pending=g_inv_request=g_inv_pending=g_popup_injected=0;
    g_quest_quit_request=g_quest_quit_pending=g_ver_request=0;
    done=polled=inventory=advances=0;
    uint32_t actor=mp_addr(0x2e7dc,0x2e5b4)+player%4*0x84;
    w32(actor+0x36,(uint32_t)player);
    w32(mp_addr(0x2ebd0,0x2e9ac),actor);
    w16(mp_addr(0x2f9e8,0x2f7a6),(uint16_t)(flags&1));
    w16(mp_addr(0x2f9f0,0x2f7ae),(uint16_t)(flags>>1));
    w16(mp_addr(0x2fa02,0x2f7bc),(uint16_t)dead);
    if(!edition)w16(0x2fa00,0);
    else w16(0x4046c,0);
    w16(mp_addr(0x2f9dc,0x2f7a0),7);
    w16(mp_addr(0x2fa08,0x2f7c2),96);
    w16(mp_addr(0x3bf74,0x3bc5c),(uint16_t)key);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,0x1ef000);
    m68k_set_reg(M68K_REG_D1,direction);
    m68k_set_reg(M68K_REG_PC,mp_addr(0x40210,0x3fedc));
    m68k_execute(10000);CHECK(done);
    return (Result){!!polled,!!inventory,r16(mp_addr(0x2f9dc,0x2f7a0)),r16(mp_addr(0x2fa02,0x2f7bc))};
}
static void matrix(void) {
    read_ram(fixture,images[0],104);read_ram(retail,images[1],104);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);native=1;
    int count=0;const int commands[]={0,0x12,0x39};
    for(int p=0;p<5;p++)for(int d=0;d<4;d++)for(int f=0;f<4;f++)
    for(int dir=0;dir<2;dir++)for(int k=0;k<3;k++) {
        Result expected=native_case(1,p,d,f,dir,commands[k],0);
        Result got=native_case(0,p,d,f,dir,commands[k],1);
        if(memcmp(&got,&expected,sizeof(got)))fprintf(stderr,"case p%d dead%d flags%d dir%d key%x got %d/%d/%d/%d retail %d/%d/%d/%d\n",p,d,f,dir,commands[k],got.poll,got.inventory,got.timer,got.counter,expected.poll,expected.inventory,expected.timer,expected.counter);
        CHECK(!memcmp(&got,&expected,sizeof(got)));CHECK(got.counter==d);count++;
        if(!d) {
            Result old=native_case(0,p,d,f,dir,commands[k],0);
            CHECK(!memcmp(&got,&old,sizeof(got)));
        }
    }
    Result old=native_case(0,2,1,0,0,0x12,0);
    CHECK(!old.poll&&old.timer==7&&old.counter==1);
    Result fixed=native_case(0,2,1,0,0,0x12,1);
    CHECK(fixed.poll&&fixed.timer==96&&fixed.counter==1);
    printf("PASS %d original-retail map-input comparisons: P1-P4/AI, dead counts, modal flags, movement, E/Space; unchanged no-death paths; former failure reproduced\n",count);
}
static SDL_Joystick *attach_pad(void) {
    SDL_VirtualJoystickDesc d={0};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
    d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;d.name="Moonstone map input test";
    int i=SDL_JoystickAttachVirtualEx(&d);CHECK(i>=0);
    SDL_Joystick *pad=SDL_JoystickOpen(i);CHECK(pad);return pad;
}
static void button(int p,SDL_GameControllerButton b,int down) {
    CHECK(SDL_JoystickSetVirtualButton(pads[p],b,(Uint8)down)==0);
}
static void key(SDL_Scancode sc,int down) {
    keys[sc]=(Uint8)down;SDL_Event e={0};e.type=down?SDL_KEYDOWN:SDL_KEYUP;
    e.key.keysym.scancode=sc;e.key.keysym.sym=SDL_GetKeyFromScancode(sc);CHECK(SDL_PushEvent(&e)==1);
}
static void setup_session(void) {
    CHECK(g_mp_campaign&&g_mp_ui_player==owner);
    int players=mp_campaign_players();mp_begin_campaign(&g_mp,players,1,1);
    for(int p=0;p<players;p++) {
        g_mp.required=1u<<p;mp_next(&g_mp);
        int device=p==owner?(keyboard_mode?MP_KEYBOARD:ids[1]):p==1&&!keyboard_mode?ids[1]:ids[0];
        CHECK(mp_claim(&g_mp,device));g_mp.phase=MP_PLAY;
    }
    g_mp.required=1u<<owner;mp_use_choices(&g_mp,owner,0);CHECK(g_mp.phase==MP_PLAY);
}
static void present(SDL_Renderer *ren) {
    int n=++presents;
    if(n==1)setup_session();
    if(n==4&&warm){CHECK(load_state(fixture));CHECK(g_mp.phase==MP_PLAY);}
    if(n==7) {
        CHECK(g_mp_ui_player==owner&&g_mp_context==MP_CAM_MAP);
        CHECK(g_mp.device[owner]==(keyboard_mode?MP_KEYBOARD:ids[1]));
        button(0,action?SDL_CONTROLLER_BUTTON_Y:SDL_CONTROLLER_BUTTON_BACK,1);
        if(!keyboard_mode)key(action?SDL_SCANCODE_I:SDL_SCANCODE_E,1);
    }
    if(n==9) {
        CHECK(!advances&&!inventory);button(0,SDL_CONTROLLER_BUTTON_BACK,0);button(0,SDL_CONTROLLER_BUTTON_Y,0);
        if(!keyboard_mode)key(action?SDL_SCANCODE_I:SDL_SCANCODE_E,0);
    }
    if(n==12) {
        if(keyboard_mode)key(action?SDL_SCANCODE_I:SDL_SCANCODE_E,1);
        else button(1,action?SDL_CONTROLLER_BUTTON_Y:SDL_CONTROLLER_BUTTON_BACK,1);
    }
    if(n==14) {
        if(keyboard_mode)key(action?SDL_SCANCODE_I:SDL_SCANCODE_E,0);
        else {button(1,SDL_CONTROLLER_BUTTON_Y,0);button(1,SDL_CONTROLLER_BUTTON_BACK,0);}
    }
    if(n==22) {
        CHECK(polled>0);
        if(action)CHECK(inventory==1&&!advances&&g_mp_ui_player==owner);
        else CHECK(advances==1&&original_index==owner);
        printf("PASS SDL P%d %s %s %s: correct owner, idle pad/keyboard rejected, native action once\n",owner+1,keyboard_mode?"keyboard":"shared pad",action?"inventory":"End Turn",warm?"warm reload":"cold load");
        SDL_Event e={.type=SDL_QUIT};CHECK(SDL_PushEvent(&e)==1);
    }
    CHECK(n<=23);SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    const char *log=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--log")&&i+1<argc)log=argv[++i];
        else if(!strcmp(argv[i],"--loadstate")&&i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--probe-retail")&&i+1<argc)retail=argv[++i];
        else if(!strcmp(argv[i],"--probe-owner")&&i+1<argc)owner=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--probe-keyboard"))keyboard_mode=1;
        else if(!strcmp(argv[i],"--probe-inventory"))action=1;
        else if(!strcmp(argv[i],"--probe-warm"))warm=1;
    }
    CHECK(log&&fixture);
    if(retail){g_log=fopen(log,"w");CHECK(g_log);matrix();fclose(g_log);return 0;}
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");SDL_SetMainReady();
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    for(int i=0;i<2;i++){pads[i]=attach_pad();ids[i]=SDL_JoystickInstanceID(pads[i]);}
    int rc=moonstone_main(argc,argv);CHECK(presents==23);return rc;
}
