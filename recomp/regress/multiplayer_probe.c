/* Test-only SDL virtual controllers against the actual host input and run loop.
 * Never packaged. Every invocation requires an explicit scratch --log path. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int probe_live, probe_frame;
static int probe_focused=1;
static int probe_pause_wait_seen, probe_paused_at;
static Uint8 probe_keys[SDL_NUM_SCANCODES];
static const Uint8 *probe_keyboard(int *count) {
    if (!probe_live) return SDL_GetKeyboardState(count);
    if (count) *count = SDL_NUM_SCANCODES;
    return probe_keys;
}
static void probe_present(SDL_Renderer *renderer);
static void campaign_guest(unsigned pc);
/* Only expose this process's virtual devices to the host under test. Physical
 * controllers can stay connected; their drivers and system settings are untouched. */
static SDL_bool probe_is_controller(int index) {
    return SDL_JoystickIsVirtual(index) && SDL_IsGameController(index);
}
#define SDL_GetKeyboardState probe_keyboard
#define SDL_RenderPresent probe_present
#define SDL_IsGameController probe_is_controller
#define moon_instr_hook probe_guest_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef SDL_GetKeyboardState
#undef SDL_RenderPresent
#undef SDL_IsGameController

void moon_instr_hook(unsigned int pc) {
    probe_guest_hook(pc);
    campaign_guest(pc);
    if (probe_live == 1 && (pc == 0x21aa4u || pc == 0x1c7574u)
        && r16(pc) == 0x4a79u) probe_pause_wait_seen = 1;
}

#define CHECK(test) do { if (!(test)) { fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#test,SDL_GetError()); exit(2); } } while (0)
typedef struct { SDL_Joystick *joy; SDL_JoystickID id; } VirtualPad;
static VirtualPad probe_a, probe_b;
static ControllerNotice probe_notice;
static const char *probe_image;
static uint64_t probe_icount;
static int probe_guest_frame;
static unsigned probe_menu_row, probe_moved_row;
static const char *probe_menu_fixture;
static int probe_entry_players, probe_entry_started;
static int probe_entry_pad_count=2, probe_entry_owner=1;
static VirtualPad probe_entry_pads[4];

static VirtualPad attach_pad(void) {
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.name = "Moonstone identical test pad";
    int index = SDL_JoystickAttachVirtualEx(&desc);
    CHECK(index >= 0 && SDL_IsGameController(index));
    SDL_Joystick *joy = SDL_JoystickOpen(index);
    CHECK(joy);
    return (VirtualPad){joy, SDL_JoystickInstanceID(joy)};
}
static void button(VirtualPad pad, SDL_GameControllerButton b, int down) {
    CHECK(SDL_JoystickSetVirtualButton(pad.joy, b, (Uint8)down) == 0);
}
static void axis(VirtualPad pad, SDL_GameControllerAxis a, Sint16 value) {
    CHECK(SDL_JoystickSetVirtualAxis(pad.joy, a, value) == 0);
}
static void detach_pad(VirtualPad *pad) {
    int index = -1;
    for (int i=0; i<SDL_NumJoysticks(); i++)
        if (SDL_JoystickGetDeviceInstanceID(i) == pad->id) index = i;
    CHECK(index >= 0 && SDL_JoystickDetachVirtual(index) == 0);
    SDL_JoystickClose(pad->joy);
    pad->joy = NULL;
}
static void events(void) {
    SDL_Event e;
    SDL_PumpEvents();
    while (SDL_PollEvent(&e)) if (e.type == SDL_CONTROLLERDEVICEADDED
        || e.type == SDL_CONTROLLERDEVICEREMOVED)
        controller_device_event(&probe_notice, &e.cdevice);
    mp_poll_pads();
}
static int update(int command, int focused, int mouse) {
    int save=0,load=0,running=1;
    events();
    return mp_update(probe_keys,command,focused,mouse,&save,&load,&running);
}
static void check_frozen(void) {
    CHECK(g_icount == probe_icount && g_cur_frame == probe_guest_frame);
    CHECK(g_mp_input[0] == 0 && g_mp_input[1] == 0);
    if (g_audio_dev) CHECK(SDL_GetAudioDeviceStatus(g_audio_dev) == SDL_AUDIO_PAUSED);
}
static void key_event(SDL_Scancode sc, SDL_Keycode sym, int down) {
    probe_keys[sc] = (Uint8)down;
    SDL_Event e = {0};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = sym;
    CHECK(SDL_PushEvent(&e) == 1);
}
static void focus_event(SDL_Renderer *ren, int focused) {
    probe_focused=focused;
    SDL_Event e = {0};
    e.type = SDL_WINDOWEVENT;
    e.window.windowID = SDL_GetWindowID(SDL_RenderGetWindow(ren));
    e.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    CHECK(SDL_PushEvent(&e) == 1);
}
static void capture_overlay(SDL_Renderer *ren) {
    if (!probe_image) return;
    int width,height;
    CHECK(SDL_GetRendererOutputSize(ren,&width,&height) == 0);
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(image && SDL_RenderReadPixels(ren,NULL,image->format->format,image->pixels,image->pitch) == 0);
    CHECK(SDL_SaveBMP(image,probe_image) == 0);
    SDL_FreeSurface(image);
}
static void capture_named_overlay(SDL_Renderer *ren, const char *suffix) {
    if (!probe_image) return;
    char path[1200];
    snprintf(path,sizeof(path),"%s-%s.bmp",probe_image,suffix);
    const char *before=probe_image;
    probe_image=path; capture_overlay(ren); probe_image=before;
}
#include "campaign_probe.h"

static void probe_prompts_present(SDL_Renderer *ren) {
    char text[64];
    const char *prompt=mp_prompt(1,text);
    int n=++probe_frame;
    if (n == 1) {
        CHECK(g_mp_practice && g_mp.phase == MP_CLAIM && g_mp.claim == 0);
        CHECK(prompt && !strcmp(prompt,"PLAYER 1 PRESS START"));
        capture_overlay(ren);
        probe_icount=g_icount; probe_guest_frame=g_cur_frame;
        button(probe_a,SDL_CONTROLLER_BUTTON_A,1); /* Held gameplay input must not block play. */
    }
    if (n >= 2 && n <= 6) check_frozen();
    if (n == 4) button(probe_b,SDL_CONTROLLER_BUTTON_START,1);
    if (n == 6) {
        CHECK(g_mp.phase == MP_RELEASE && g_mp.device[0] == probe_b.id
              && g_mp.device[1] == probe_a.id && g_primary_pad == probe_b.id);
        CHECK(!prompt && !campaign_text_pixels(ren));
        capture_named_overlay(ren,"accepted-start");
        button(probe_b,SDL_CONTROLLER_BUTTON_START,0);
    }
    if (n == 10) {
        CHECK(g_mp.phase == MP_PLAY && g_mp.device[1] == probe_a.id);
        CHECK(g_icount>probe_icount && g_mp_input[1]==16 && !g_pause_request);
        CHECK(!prompt && !campaign_text_pixels(ren));
    }
    if (n == 30) {
        CHECK(g_mp.phase==MP_PLAY && g_icount>probe_icount && g_mp_input[1]==16);
        CHECK(!prompt && !campaign_text_pixels(ren));
        capture_named_overlay(ren,"held-gameplay");
    }
    if (n == 36) button(probe_a,SDL_CONTROLLER_BUTTON_A,0);
    if (n == 40) {
        CHECK(g_mp.phase == MP_PLAY && g_icount > probe_icount && !prompt);
        capture_named_overlay(ren,"playing");
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
        button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
        key_event(SDL_SCANCODE_UP,SDLK_UP,1); /* Keyboard belongs to neither. */
    }
    if (n == 46) {
        CHECK(g_mp_input[0] == 2 && g_mp_input[1] == 1);
        CHECK(r16(0x2e7dc+0x3e) == 2 && r16(0x2e860+0x3e) == 1);
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);
        button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);
        key_event(SDL_SCANCODE_UP,SDLK_UP,0);
    }
    if (n == 48) {
        /* Returning to the original menu must discard Practice assignment UI. */
        CHECK(probe_menu_fixture && load_state(probe_menu_fixture));
        CHECK(!g_mp_practice && g_mp.phase == MP_OFF && !mp_prompt(1,text));
        probe_icount=g_icount;
    }
    if (n == 52) {
        CHECK(!g_mp_practice && g_icount > probe_icount && !prompt);
        SDL_Event e = {.type=SDL_QUIT}; SDL_PushEvent(&e);
        puts("PASS: Practice consumes P1 Start only, then runs with P2 attack held and no release prompt; both pads work; warm menu load clears setup");
    }
    CHECK(n < 56);
    SDL_RenderPresent(ren);
}
static void probe_solo_present(SDL_Renderer *ren) {
    char text[64];
    int n=++probe_frame;
    CHECK(!g_mp_practice && g_mp.phase == MP_OFF);
    CHECK(!mp_prompt(1,text) && !mp_prompt(0,text));
    if (n == 1) {
        CHECK(mp_practice_snapshot() && mp_pad_count() == 0);
        probe_icount=g_icount;
        key_event(SDL_SCANCODE_LEFT,SDLK_LEFT,1);
    }
    if (n == 8) {
        CHECK(g_icount > probe_icount && r16(0x2e7dc+0x3e) == 2);
        CHECK(r16(0x2e860+0x3e) == 0 && mp_pad_count() == 0);
        key_event(SDL_SCANCODE_LEFT,SDLK_LEFT,0);
        key_event(SDL_SCANCODE_RIGHT,SDLK_RIGHT,1);
    }
    if (n == 16) {
        CHECK(r16(0x2e7dc+0x3e) == 1 && r16(0x2e860+0x3e) == 0);
        key_event(SDL_SCANCODE_RIGHT,SDLK_RIGHT,0);
    }
    if (n == 20) {
        CHECK(!mp_prompt(1,text));
        key_event(SDL_SCANCODE_F5,SDLK_F5,1);
    }
    if (n == 21) key_event(SDL_SCANCODE_F5,SDLK_F5,0);
    if (n == 23) key_event(SDL_SCANCODE_F9,SDLK_F9,1);
    if (n == 24) key_event(SDL_SCANCODE_F9,SDLK_F9,0);
    if (n == 28) key_event(SDL_SCANCODE_SPACE,SDLK_SPACE,1);
    if (n == 29) key_event(SDL_SCANCODE_SPACE,SDLK_SPACE,0);
    if (n == 38) key_event(SDL_SCANCODE_SPACE,SDLK_SPACE,1);
    if (n == 39) key_event(SDL_SCANCODE_SPACE,SDLK_SPACE,0);
    if (n == 44) {
        capture_overlay(ren);
        SDL_Event e = {.type=SDL_QUIT}; SDL_PushEvent(&e);
        puts("PASS: keyboard-only Practice starts without setup; movement, F9 and original pause work without assignment prompts; green stays idle");
    }
    CHECK(n < 48);
    SDL_RenderPresent(ren);
}
static void probe_entry_present(SDL_Renderer *ren) {
    int n=++probe_frame;
    VirtualPad p1=probe_entry_pads[probe_entry_owner];
    int second=probe_entry_owner==0?1:0;
    int p2=probe_entry_pad_count==1?MP_KEYBOARD:probe_entry_pads[second].id;
    if (n == 1) CHECK(g_menu_live && !g_mp_practice && r16(0x2e024) == 1);
    if (n == 4 && probe_entry_players == 2) button(p1,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    if (n == 5 && probe_entry_players == 2) button(p1,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);
    if (n == 20) button(p1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
    if (n == 70) button(p1,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);
    if (n == 85) button(p1,SDL_CONTROLLER_BUTTON_DPAD_UP,1);
    if (n == 95) button(p1,SDL_CONTROLLER_BUTTON_DPAD_UP,0);
    if (n == 100) CHECK(r16(0x3051e) == 2 && r16(0x2e024) == probe_entry_players);
    if (n == 105) button(p1,SDL_CONTROLLER_BUTTON_A,1);
    if (n == 111) button(p1,SDL_CONTROLLER_BUTTON_A,0);
    if(g_mp_practice) {
        char text[64];
        CHECK(g_mp.phase==MP_PLAY && !mp_prompt(1,text));
        CHECK(g_primary_pad==p1.id && g_mp.device[0]==p1.id && g_mp.device[1]==p2);
    }
    if (!probe_entry_started && n > 111 && (g_mp_practice || g_combat_pause_live)) {
        CHECK(r16(0x2e064) == probe_entry_players);
        CHECK(r16(0x2e024) == 2); /* Practice sets its own two-player count. */
        CHECK(g_mp_practice);
        capture_overlay(ren);
        probe_entry_started=n;
    }
    int t=probe_entry_started ? n-probe_entry_started : -1;
    if (t == 8) {
        CHECK(g_mp.phase == MP_PLAY);
        button(p1,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
        if(p2==MP_KEYBOARD)key_event(SDL_SCANCODE_RIGHT,SDLK_RIGHT,1);
        else button(probe_entry_pads[second],SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    }
    /* No Start is pressed in this test. Wait for both
     * original actor input readers, with a hard deadline if either stays idle. */
    if (t >= 16 && probe_entry_players && r16(0x2e7dc+0x3e) == 2
        && r16(0x2e860+0x3e) == 1) {
        CHECK(g_mp_input[0] == 2 && g_mp_input[1] == 1);
        CHECK(r16(0x2e7dc+0x3e) == 2 && r16(0x2e860+0x3e) == 1);
        capture_named_overlay(ren,"both-players");
        char path[1200],text[64];snprintf(path,sizeof(path),"%s.sav",probe_image);
        CHECK(save_state(path) && load_state(path)); /* warm F9 keeps assignments */
        CHECK(g_mp.phase==MP_PLAY && g_mp.device[0]==p1.id && g_mp.device[1]==p2 && !mp_prompt(1,text));
        CHECK(probe_menu_fixture && load_state(probe_menu_fixture));
        CHECK(!g_mp_practice && g_mp.phase==MP_OFF && g_primary_pad==p1.id);
        CHECK(load_state(path)); /* known menu owner also supplies a loaded Practice */
        CHECK(g_mp.phase==MP_PLAY && g_mp.device[0]==p1.id && g_mp.device[1]==p2 && !mp_prompt(1,text));
        SDL_Event e = {.type=SDL_QUIT}; SDL_PushEvent(&e);
        printf("PASS: campaign Players=%d, %d pads, menu owner #%d enters Practice without Start or a popup; both original fighters and warm/menu loads work\n",
               probe_entry_players,probe_entry_pad_count,probe_entry_owner+1);
        probe_entry_players=0; /* mark the assertion complete, avoiding a second report */
    }
    CHECK(t < 500 || !probe_entry_players);
    CHECK(n < 1000);
    SDL_RenderPresent(ren);
}
static void probe_campaign_present(SDL_Renderer *ren) {
    char text[64];
    int n=++probe_frame;
    CHECK(!mp_practice_snapshot() && !g_mp_practice && g_mp.phase == MP_OFF);
    CHECK(!mp_prompt(1,text) && !mp_prompt(0,text));
    if (n == 1) {
        CHECK(r16(0x2e024) == 1 && mp_pad_count() == 2);
        probe_icount=g_icount;
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    }
    if (n == 8) {
        CHECK(g_icount > probe_icount && g_primary_pad == probe_b.id && g_ji_rt);
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);
        button(probe_a,SDL_CONTROLLER_BUTTON_START,1);
    }
    if (n == 12) detach_pad(&probe_b);
    if (n == 16) button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
    if (n == 24) {
        CHECK(g_primary_pad == probe_a.id && g_ji_lf);
        SDL_Event e = {.type=SDL_QUIT}; SDL_PushEvent(&e);
        puts("PASS: solo campaign with two pads and disconnect recovery keeps normal menu input without Practice assignment prompts");
    }
    CHECK(n < 28);
    SDL_RenderPresent(ren);
}
static void probe_menu_present(SDL_Renderer *ren) {
    int n = ++probe_frame;
    if (n == 1) {
        CHECK(g_menu_live && !g_mp_practice && mp_pad_count() == 2);
        probe_menu_row = r16(0x3051e);
    }
    if (n == 4) button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
    if (n == 12) {
        /* B was enumerated second, but it is the controller the user chose. */
        CHECK(g_ji_dn && r16(0x3051e) != probe_menu_row);
        probe_moved_row = r16(0x3051e);
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);
    }
    if (n == 15) button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_UP,1);
    if (n == 19) {
        CHECK(!g_ji_up && r16(0x3051e) == probe_moved_row);
        button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_UP,0);
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_UP,1);
    }
    if (n == 27) {
        CHECK(g_ji_up && r16(0x3051e) == probe_menu_row);
        button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_UP,0);
        capture_overlay(ren);
        SDL_Event e = {.type=SDL_QUIT}; SDL_PushEvent(&e);
        puts("PASS: second-enumerated controller navigates the original main menu; ownership stays stable");
    }
    CHECK(n < 32);
    SDL_RenderPresent(ren);
}
static void probe_present_dispatch(SDL_Renderer *ren) {
    if (!probe_live) { SDL_RenderPresent(ren); return; }
    if (probe_live >= 7) { campaign_present(ren); return; }
    if (probe_live == 2) { probe_menu_present(ren); return; }
    if (probe_live == 3) { probe_prompts_present(ren); return; }
    if (probe_live == 4) { probe_solo_present(ren); return; }
    if (probe_live == 5) { probe_entry_present(ren); return; }
    if (probe_live == 6) { probe_campaign_present(ren); return; }
    int n = ++probe_frame;
    if (n == 1) {
        CHECK(mp_practice_snapshot() && !g_mp_practice && g_mp.phase == MP_OFF);
        capture_overlay(ren);
        probe_icount=g_icount; probe_guest_frame=g_cur_frame;
        probe_a=attach_pad();
    }
    if (n >= 2 && n <= 6) check_frozen();
    if (n == 4) button(probe_a,SDL_CONTROLLER_BUTTON_START,1);
    if (n == 6) {
        CHECK(g_mp.phase == MP_RELEASE && g_mp.device[0] == probe_a.id
              && g_mp.device[1] == MP_KEYBOARD);
        key_event(SDL_SCANCODE_LEFT,SDLK_LEFT,1);
        button(probe_a,SDL_CONTROLLER_BUTTON_START,0);
    }
    if (n == 12) {
        CHECK(g_mp.phase==MP_PLAY && g_icount>probe_icount && g_mp_input[1]==2);
        key_event(SDL_SCANCODE_LEFT,SDLK_LEFT,0);
    }
    if (n == 15) {
        CHECK(g_mp.phase == MP_PLAY && g_icount > probe_icount);
        /* The fixture is silent. Also exercise suspension of an already-running
         * audio device, in addition to the initial pending-first-sound state. */
        CHECK(g_audio_dev);
        g_audio_paused=0;
        SDL_PauseAudioDevice(g_audio_dev,0);
        key_event(SDL_SCANCODE_RIGHT,SDLK_RIGHT,1);
        button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
    }
    if (n == 20) {
        CHECK(g_mp_input[0] == 2 && g_mp_input[1] == 1);
        CHECK(r16(0x2e7dc+0x3e) == 2 && r16(0x2e860+0x3e) == 1);
        focus_event(ren,0);
        probe_icount=g_icount; probe_guest_frame=g_cur_frame;
    }
    if (n >= 21 && n <= 25) check_frozen();
    if (n == 25) focus_event(ren,1);
    if (n == 28) {
        CHECK(g_mp.phase == MP_PLAY && g_icount > probe_icount);
        CHECK(SDL_GetAudioDeviceStatus(g_audio_dev) == SDL_AUDIO_PLAYING);
        CHECK(g_mp_input[0]==2 && g_mp_input[1]==1);
        key_event(SDL_SCANCODE_RIGHT,SDLK_RIGHT,0);
        button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);
        detach_pad(&probe_a);
        probe_icount=g_icount; probe_guest_frame=g_cur_frame;
    }
    if (n >= 29 && n <= 35) check_frozen();
    if (n == 31) {
        CHECK(g_mp.phase == MP_CLAIM && g_mp.claim == 0 && g_mp.device[1] == MP_KEYBOARD);
        probe_b=attach_pad();
    }
    if (n == 33) button(probe_b,SDL_CONTROLLER_BUTTON_START,1);
    if (n == 35) button(probe_b,SDL_CONTROLLER_BUTTON_START,0);
    if (n == 40) {
        CHECK(g_mp.phase == MP_PLAY && g_mp.device[0] == probe_b.id
              && g_mp.device[1] == MP_KEYBOARD && g_icount > probe_icount);
        button(probe_b,SDL_CONTROLLER_BUTTON_A,1);
    }
    if (n == 44) button(probe_b,SDL_CONTROLLER_BUTTON_A,0);
    if (n == 46) key_event(SDL_SCANCODE_F5,SDLK_F5,1);
    if (n == 47) key_event(SDL_SCANCODE_F5,SDLK_F5,0);
    if (n == 49) key_event(SDL_SCANCODE_LEFT,SDLK_LEFT,1);
    if (n == 52) key_event(SDL_SCANCODE_F9,SDLK_F9,1);
    if (n == 53) {
        CHECK(g_mp.phase == MP_PLAY && g_mp.device[0] == probe_b.id
              && g_mp.device[1] == MP_KEYBOARD);
        probe_icount=g_icount; probe_guest_frame=g_cur_frame;
    }
    if (n == 57) {
        CHECK(g_icount>probe_icount && g_mp_input[1]==2);
        key_event(SDL_SCANCODE_LEFT,SDLK_LEFT,0);
        key_event(SDL_SCANCODE_F9,SDLK_F9,0);
    }
    if (n == 60) {
        CHECK(g_mp.phase == MP_PLAY && g_icount > probe_icount);
        CHECK(SDL_GetAudioDeviceStatus(g_audio_dev) == SDL_AUDIO_PLAYING);
        MpSession before=g_mp;
        uint64_t instructions=g_icount;
        CHECK(!load_state("nonexistent-multiplayer-probe-save.sav"));
        CHECK(g_icount == instructions && g_mp.device[0] == before.device[0]
              && g_mp.device[1] == before.device[1] && g_mp.phase == MP_PLAY);
        button(probe_b,SDL_CONTROLLER_BUTTON_START,1); /* original combat pause */
    }
    if (n == 61) button(probe_b,SDL_CONTROLLER_BUTTON_START,0);
    /* Let the original combat routine enter its pause wait before deliberately
     * losing focus, which correctly clears any still-pending host shortcut. */
    if (n >= 64 && probe_pause_wait_seen && !probe_paused_at) {
        probe_paused_at=n;
        focus_event(ren,0);
        probe_icount=g_icount; probe_guest_frame=g_cur_frame;
    }
    int t=probe_paused_at ? n-probe_paused_at : -1;
    if (t >= 1 && t <= 4) check_frozen();
    if (t == 4) focus_event(ren,1);
    if (t == 6) button(probe_b,SDL_CONTROLLER_BUTTON_START,1); /* original resume */
    if (t == 7) button(probe_b,SDL_CONTROLLER_BUTTON_START,0);
    if (t == 10) {
        SDL_Event e = {.type=SDL_QUIT}; SDL_PushEvent(&e);
        printf("PASS: setup/focus/disconnect pause correctly; held input resumes after identification, focus return and F9; audio and assignments survive; original pause and failed load work\n");
    }
    CHECK(n < 180);
    SDL_RenderPresent(ren);
}
static void probe_present(SDL_Renderer *ren) {
    if (probe_live>=7 && probe_focused) campaign_scene_check(ren);
    probe_present_dispatch(ren);
}
static void menu_device_tests(void) {
    g_mp_practice=0;
    mp_reset(&g_mp);
    probe_a=attach_pad(); probe_b=attach_pad();
    button(probe_a,SDL_CONTROLLER_BUTTON_A,1);
    SDL_JoystickUpdate();
    controller_open_all(&probe_notice); events();
    CHECK(mp_pad_count() == 2 && !controller_primary(1)); /* held on startup */
    button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1); events();
    CHECK(!controller_primary(0)); /* background input cannot claim */
    events(); CHECK(!controller_primary(1)); /* focus does not make it fresh */
    button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0); events();
    button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1); events();
    CHECK(controller_primary(1) == mp_pad(probe_b.id));
    CHECK(controller_primary(1)->now[CONTROL_DOWN]);
    button(probe_b,SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);
    button(probe_a,SDL_CONTROLLER_BUTTON_START,1); events();
    CHECK(controller_primary(1)->id == probe_b.id);
    detach_pad(&probe_a); events();
    CHECK(controller_primary(1)->id == probe_b.id);
    /* Reusing a lower registry slot must not promote the new controller. */
    probe_a=attach_pad(); events();
    button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_UP,1); events();
    CHECK(controller_primary(1)->id == probe_b.id);
    detach_pad(&probe_b); events();
    CHECK(!controller_primary(1)); /* remaining held pad must release first */
    button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_UP,0); events();
    button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_UP,1); events();
    CHECK(controller_primary(1)->id == probe_a.id);
    probe_b=attach_pad(); events();
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1); events();
    CHECK(controller_primary(1)->id == probe_a.id); /* first-enumerated also works */
    detach_pad(&probe_b); events();
    CHECK(controller_primary(1)->id == probe_a.id);
    detach_pad(&probe_a); events();
    CHECK(!controller_primary(1) && !mp_pad_count());
    puts("PASS: menu selection ignores enumeration order and held/background input; hotplug, slot reuse and disconnect recovery preserve ownership");
}
static void initial_assignment_tests(void) {
    char text[64];
    /* Every possible menu owner with one through four pads, including a held
     * P2 attack. Choose the menu owner using real SDL input, never enumeration. */
    for(int count=1;count<=4;count++)for(int first=0;first<count;first++) {
        VirtualPad pads[4];g_mp_practice=0;mp_reset(&g_mp);
        for(int i=0;i<count;i++)pads[i]=attach_pad();events();
        button(pads[first],SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);events();
        CHECK(controller_primary(1)->id==pads[first].id);
        button(pads[first],SDL_CONTROLLER_BUTTON_DPAD_DOWN,0);events();
        int second=first==0?1:0,p2=count==1?MP_KEYBOARD:pads[second].id;
        if(count==1)probe_keys[SDL_SCANCODE_LCTRL]=1;
        else button(pads[second],SDL_CONTROLLER_BUTTON_A,1);
        events();g_mp_practice=1;mp_begin_practice();
        CHECK(g_mp.phase==MP_PLAY && !mp_prompt(1,text));
        CHECK(g_mp.device[0]==pads[first].id && g_mp.device[1]==p2 && g_mp.enrolled==3);
        CHECK(!update(0,1,0) && !g_pause_request && g_mp_input[0]==0 && g_mp_input[1]==16);
        CHECK(update(0,0,0) && !strcmp(mp_prompt(0,text),"PAUSED"));
        CHECK(!update(0,1,0) && g_mp_input[1]==16 && g_primary_pad==pads[first].id);
        memset(probe_keys,0,sizeof(probe_keys));
        for(int i=0;i<count;i++)detach_pad(&pads[i]);events();
    }
    /* Unknown owner after a cold load: one P1 Start also assigns P2 with spare
     * pads present. A later loss still requires explicit recovery. */
    g_mp_practice=1;mp_reset(&g_mp);
    probe_a=attach_pad(); probe_b=attach_pad();
    VirtualPad extra=attach_pad(); events();mp_begin_practice();
    CHECK(g_mp.phase==MP_CLAIM && !strcmp(mp_prompt(1,text),"PLAYER 1 PRESS START"));
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1);
    button(probe_a,SDL_CONTROLLER_BUTTON_A,1);
    button(extra,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,1,0) && g_mp.phase==MP_RELEASE && g_mp.device[0]==probe_b.id && g_mp.device[1]==probe_a.id);
    CHECK(!mp_prompt(1,text) && !g_pause_request);
    button(probe_b,SDL_CONTROLLER_BUTTON_START,0);
    CHECK(!update(0,1,0) && g_mp_input[1]==16);
    detach_pad(&probe_a);
    CHECK(update(0,1,0) && g_mp.claim == 1 && g_mp.device[1] == MP_NONE);
    CHECK(update(0,1,0) && g_mp.device[0] == probe_b.id); /* No auto-replacement. */
    button(extra,SDL_CONTROLLER_BUTTON_START,0);CHECK(update(0,1,0));
    button(extra,SDL_CONTROLLER_BUTTON_START,1); CHECK(update(0,1,0));
    button(extra,SDL_CONTROLLER_BUTTON_START,0); CHECK(!update(0,1,0));
    CHECK(g_mp.device[1] == extra.id && g_mp.device[0] == probe_b.id);
    detach_pad(&extra); detach_pad(&probe_b); events(); mp_reset(&g_mp);
    puts("PASS: ten menu-owner/pad-count combinations inherit P1 and next pad/keyboard without prompts; unknown owners need only P1 Start; held input, focus and explicit recovery work");
}
static void confirmation_device_tests(void) {
    g_mp_practice=0;g_mp_campaign=1;g_mp_context=MP_CAM_SELECT;g_mp_ui_player=1;
    probe_a=attach_pad();probe_b=attach_pad();VirtualPad extra=attach_pad();events();
    mp_begin_campaign(&g_mp,2,0,3);
    CHECK(mp_claim(&g_mp,probe_a.id) && mp_claim(&g_mp,probe_b.id));
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1);events(); /* already held at handoff */
    g_mp.required=2;g_mp.claim=1;g_mp.phase=MP_CONFIRM;
    CHECK(update(0,1,0) && g_mp.phase==MP_CONFIRM);
    button(extra,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,1,0) && g_mp.phase==MP_CONFIRM && g_mp.device[1]==probe_b.id);
    button(probe_b,SDL_CONTROLLER_BUTTON_START,0);update(0,1,0);
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,0,0) && g_mp.phase==MP_CONFIRM);
    CHECK(update(0,1,0) && g_mp.phase==MP_CONFIRM); /* focus cannot make it fresh */
    detach_pad(&probe_b);
    CHECK(update(0,1,0) && g_mp.phase==MP_CLAIM && g_mp.claim==1);
    CHECK(g_mp.device[0]==probe_a.id && g_mp.device[1]==MP_NONE);
    CHECK(update(0,1,0) && g_mp.device[1]==MP_NONE); /* held spare cannot replace */
    button(extra,SDL_CONTROLLER_BUTTON_START,0);update(0,1,0);
    button(extra,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,1,0) && g_mp.phase==MP_RELEASE);
    button(extra,SDL_CONTROLLER_BUTTON_START,0);
    CHECK(!update(0,1,0) && g_mp.device[0]==probe_a.id && g_mp.device[1]==extra.id);
    CHECK(!g_pause_request && !g_mp_input[0] && !g_mp_input[1]);
    mp_campaign_clear();mp_reset(&g_mp);
    detach_pad(&probe_a);detach_pad(&extra);events();
    puts("PASS: selection confirmation rejects held/background/spare-pad Start; required-device recovery retains the other player and consumes Start");
}
static void mixed_campaign_device_tests(void) {
    g_mp_practice=0;g_mp_campaign=1;g_mp_context=MP_CAM_SELECT;g_mp_ui_player=0;
    VirtualPad pads[4];for(int p=0;p<4;p++) pads[p]=attach_pad();events();
    for(int keyboard_player=0;keyboard_player<4;keyboard_player++) {
        mp_begin_campaign(&g_mp,4,0,15);
        for(int p=0;p<4;p++) {
            CHECK(g_mp.phase==MP_CLAIM && g_mp.claim==p);
            if(p==keyboard_player) {
                CHECK(mp_keyboard_claimable());
                probe_keys[SDL_SCANCODE_RETURN]=1;CHECK(update(1,1,0));
                probe_keys[SDL_SCANCODE_RETURN]=0;update(0,1,0);
                CHECK(g_mp.device[p]==MP_KEYBOARD);
            } else {
                /* Simultaneous Enter belongs to this prompt, not the next one. */
                if(p==0) probe_keys[SDL_SCANCODE_RETURN]=1;
                button(pads[p],SDL_CONTROLLER_BUTTON_START,1);CHECK(update(p==0,1,0));
                CHECK(g_mp.device[p]==pads[p].id);
                if(p<3) CHECK(g_mp.device[p+1]==MP_NONE);
                probe_keys[SDL_SCANCODE_RETURN]=0;
                button(pads[p],SDL_CONTROLLER_BUTTON_START,0);update(0,1,0);
            }
        }
        CHECK(g_mp.phase==MP_PLAY && !g_mp.shared && mp_owner(&g_mp,MP_KEYBOARD)==keyboard_player);
        g_mp_ui_player=keyboard_player;g_mp.required=1u<<keyboard_player;
        CHECK(!update(0,1,0)); /* spare controller must not offer to replace the keyboard */
        VirtualPad extra=attach_pad();CHECK(!update(0,1,0));
        button(extra,SDL_CONTROLLER_BUTTON_START,1);CHECK(!update(0,1,0));
        CHECK(g_mp.device[keyboard_player]==MP_KEYBOARD);
        detach_pad(&extra);CHECK(!update(0,1,0));
        int missing=(keyboard_player+1)%4;
        int prior[4];memcpy(prior,g_mp.device,sizeof(prior));
        g_mp_context=MP_CAM_COMBAT;g_mp_combatants=(1u<<missing)|(1u<<keyboard_player);
        g_mp.required=g_mp_combatants;
        detach_pad(&pads[missing]);CHECK(update(0,1,0));
        CHECK(g_mp.phase==MP_CLAIM && g_mp.claim==missing && !mp_keyboard_claimable());
        probe_keys[SDL_SCANCODE_RETURN]=1;CHECK(update(1,1,0));
        CHECK(g_mp.device[missing]==MP_NONE && g_mp.device[keyboard_player]==MP_KEYBOARD);
        probe_keys[SDL_SCANCODE_RETURN]=0;
        pads[missing]=attach_pad();button(pads[missing],SDL_CONTROLLER_BUTTON_START,1);
        CHECK(update(0,1,0) && g_mp.device[missing]==MP_NONE); /* held on reconnect */
        button(pads[missing],SDL_CONTROLLER_BUTTON_START,0);CHECK(update(0,1,0));
        button(pads[missing],SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
        button(pads[missing],SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
        for(int p=0;p<4;p++) CHECK(g_mp.device[p]==(p==missing?pads[p].id:prior[p]));
        CHECK(!g_pause_request);
        g_mp_context=MP_CAM_SELECT;g_mp_ui_player=0;
    }
    mp_campaign_clear();mp_reset(&g_mp);
    for(int p=0;p<4;p++) detach_pad(&pads[p]);events();
    /* Actual initial choices decide sharing, even if a device drops out before
     * the last choice. Keyboard borrowing is unavailable on ordinary turns. */
    MpSession s;
    mp_begin_campaign(&s,3,1,1);CHECK(mp_claim(&s,10));
    s.required=2;mp_next(&s);CHECK(mp_claim(&s,20));
    mp_removed(&s,10);
    s.required=4;mp_next(&s);CHECK(mp_claim(&s,MP_KEYBOARD));
    CHECK(!s.shared && s.enrolled==7 && s.device[0]==MP_NONE);
    s.required=1;mp_next(&s);CHECK(!mp_claim(&s,MP_KEYBOARD));CHECK(mp_claim(&s,11));
    mp_begin_campaign(&s,3,1,1);CHECK(mp_claim(&s,10));
    s.required=2;mp_next(&s);CHECK(mp_claim(&s,20));
    s.required=4;mp_next(&s);CHECK(mp_claim(&s,10));
    CHECK(s.shared && s.shared_used && s.enrolled==7);
    s.required=1;mp_next(&s);CHECK(!mp_claim(&s,MP_KEYBOARD));CHECK(mp_claim(&s,20));
    CHECK(s.shared); /* a later spare/replacement does not silently change mode */
    mp_begin_campaign(&s,3,1,1);CHECK(mp_claim(&s,MP_KEYBOARD));
    s.required=2;mp_next(&s);CHECK(mp_claim(&s,10));
    s.required=4;mp_next(&s);CHECK(mp_claim(&s,10));
    s.required=6;s.device[1]=s.device[2]=MP_NONE;mp_next(&s);
    CHECK(mp_claim(&s,10));
    CHECK(s.phase==MP_CLAIM && s.claim==2 && !mp_claim(&s,MP_KEYBOARD) && !mp_claim(&s,10));
    CHECK(s.device[0]==MP_KEYBOARD && mp_claim(&s,20) && s.shared);
    puts("PASS: any campaign player can choose keyboard with four pads; one keyboard owner, no hotplug takeover/fallback, fresh reconnect, simultaneous claim isolation and stable shared/personal choices");
}
static void scene_input_tests(void) {
    char text[64];
    /* Minimal original-campaign recognition fixture for the real context hook. */
    int prior_os=g_os,prior_sdl=g_sdl_mode,prior_lineage=g_lineage;
    uint16_t prior_op=r16(0x22fc4),prior_menu=r16(0x3051e),prior_players=r16(0x2e024);
    uint32_t prior_operand=r32(0x22fc6);
    g_os=g_sdl_mode=1;g_lineage=LIN_CRACKED;
    w16(0x22fc4,0x0c28);w32(0x22fc6,0x0001000b);w16(0x3051e,3);w16(0x2e024,2);
    g_mp_practice=0;g_mp_campaign=1;g_mp_context=MP_CAM_COMBAT;
    g_mp_ui_player=0;g_mp_combatants=3;
    probe_a=attach_pad();events();mp_begin_campaign(&g_mp,2,0,3);
    CHECK(mp_claim(&g_mp,probe_a.id));
    probe_keys[SDL_SCANCODE_RETURN]=1;
    CHECK(update(1,1,0) && g_mp.phase==MP_RELEASE && !mp_prompt(1,text));
    CHECK(g_mp.device[1]==MP_KEYBOARD && !g_mp_input[0] && !g_mp_input[1]);
    button(probe_a,SDL_CONTROLLER_BUTTON_A,1);
    probe_keys[SDL_SCANCODE_LCTRL]=1;
    probe_keys[SDL_SCANCODE_RETURN]=0;
    CHECK(!update(0,1,0) && g_mp_input[0]==16 && g_mp_input[1]==16);
    button(probe_a,SDL_CONTROLLER_BUTTON_A,0);memset(probe_keys,0,sizeof(probe_keys));
    CHECK(!update(0,1,0));
    for(int held=0;held<6;held++) {
        if(held==0) button(probe_a,SDL_CONTROLLER_BUTTON_A,1);
        if(held==1) axis(probe_a,SDL_CONTROLLER_AXIS_LEFTX,32767);
        if(held==2) axis(probe_a,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);
        if(held==3) probe_keys[SDL_SCANCODE_LCTRL]=1;
        if(held==4) probe_keys[SDL_SCANCODE_RETURN]=1;
        for(int scene=0;scene<4;scene++) {
            MpCampaignContext contexts[]={MP_CAM_MAP,MP_CAM_COMBAT,MP_CAM_UI,MP_CAM_MAP};
            int owner=scene==0?1:0;
            mp_campaign_context(contexts[scene],owner,scene==1?3:0);
            CHECK(g_mp.phase==MP_PLAY && !mp_prompt(1,text));
            CHECK(!update(0,1,held==5));
            unsigned pad=held<3?(held==1?1:16):0,keys=held>=3?16:0;
            CHECK(g_mp_input[0]==((g_mp.required&1)?pad:0));
            CHECK(g_mp_input[1]==((g_mp.required&2)?keys:0));
            CHECK(g_mp.device[0]==probe_a.id && g_mp.device[1]==MP_KEYBOARD);
        }
        CHECK(update(0,0,held==5) && !strcmp(mp_prompt(0,text),"PAUSED"));
        CHECK(!update(0,1,held==5) && !mp_prompt(1,text));
        button(probe_a,SDL_CONTROLLER_BUTTON_A,0);
        axis(probe_a,SDL_CONTROLLER_AXIS_LEFTX,0);
        axis(probe_a,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);
        memset(probe_keys,0,sizeof(probe_keys));CHECK(!update(0,1,0));
    }
    mp_campaign_clear();mp_reset(&g_mp);
    detach_pad(&probe_a);events();
    w16(0x22fc4,prior_op);w32(0x22fc6,prior_operand);
    w16(0x3051e,prior_menu);w16(0x2e024,prior_players);
    g_os=prior_os;g_sdl_mode=prior_sdl;g_lineage=prior_lineage;
    puts("PASS: 24 real campaign context/input combinations continue with held button/stick/trigger/keyboard/Enter/mouse; identification consumes only Enter; focus return and ownership work");
}
static void shared_campaign_tests(void) {
    uint8_t *ram=malloc(RAM_SIZE);CHECK(ram);memcpy(ram,g_ram,RAM_SIZE);
    int prior_os=g_os,prior_sdl=g_sdl_mode,prior_lineage=g_lineage;
    g_os=g_sdl_mode=1;g_lineage=LIN_CRACKED;g_mp_practice=0;
    w16(0x22fc4,0x0c28);w32(0x22fc6,0x0001000b);w16(0x3051e,3);w16(0x2e024,4);
    for(int p=0;p<4;p++)w32(0x2e7dc+p*0x84+0x36,3-p);
    VirtualPad shared=attach_pad();events();mp_begin_campaign(&g_mp,4,1,1);
    g_mp_campaign=1;g_mp_context=MP_CAM_SELECT;g_mp_ui_player=0;
    for(int p=0;p<4;p++) {
        g_mp_ui_player=p;
        g_mp.required=1u<<p;mp_next(&g_mp);
        if(p==3)probe_keys[SDL_SCANCODE_RETURN]=1;
        else button(shared,SDL_CONTROLLER_BUTTON_START,1);
        CHECK(update(p==3,1,0));
        button(shared,SDL_CONTROLLER_BUTTON_START,0);memset(probe_keys,0,sizeof(probe_keys));
        CHECK(!update(0,1,0));
    }
    char text[64];
    for(int round=0;round<2;round++)for(int p=0;p<4;p++) {
        mp_campaign_context(MP_CAM_MAP,p,0);
        CHECK(!update(0,1,0) && !mp_prompt(1,text));
        CHECK(g_mp.device[p]==(p==3?MP_KEYBOARD:shared.id));
        w32(0x29f16,32);w32(0x2e0bc,0x2e7dc+p*0x84);
        w32(0x2e0c0,0x2e7dc+((p+3)%4)*0x84);mp_campaign_combat();
        CHECK(!update(0,1,0) && g_mp.required==1u<<p && !mp_prompt(1,text));
        mp_campaign_context(MP_CAM_UI,p,0);CHECK(!update(0,1,0) && !mp_prompt(1,text));
    }
    /* P4 can finish their keyboard turn while the shared controller is absent. */
    detach_pad(&shared);CHECK(!update(0,1,0) && g_mp.disconnected==7);
    mp_campaign_context(MP_CAM_MAP,2,0);
    CHECK(update(1,1,0) && g_mp.claim==2 && !mp_keyboard_claimable());
    shared=attach_pad();button(shared,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,1,0) && g_mp.device[2]==MP_NONE);
    button(shared,SDL_CONTROLLER_BUTTON_START,0);CHECK(update(0,1,0));
    button(shared,SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
    button(shared,SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
    CHECK(!g_mp.disconnected && g_primary_pad==shared.id);
    for(int p=0;p<4;p++) {
        mp_campaign_context(MP_CAM_MAP,p,0);CHECK(!update(0,1,0));
        CHECK(g_mp.device[p]==(p==3?MP_KEYBOARD:shared.id));
    }
    /* P3 keeps the active pad; P2 needs a distinct pad only for this duel. */
    mp_campaign_context(MP_CAM_MAP,2,0);CHECK(!update(0,1,0));
    mp_campaign_context(MP_CAM_COMBAT,2,6);CHECK(update(0,1,0) && g_mp.claim==1);
    button(shared,SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0) && g_mp.device[1]==MP_NONE);
    button(shared,SDL_CONTROLLER_BUTTON_START,0);CHECK(update(0,1,0));
    VirtualPad spare=attach_pad();events();
    button(spare,SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
    button(spare,SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
    button(shared,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);button(spare,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    CHECK(!update(0,1,0) && g_mp_input[2]==2 && g_mp_input[1]==1);
    CHECK(g_mp.chosen[1]==shared.id && g_mp.device[1]==spare.id);
    button(shared,SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);button(spare,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);
    mp_campaign_context(MP_CAM_UI,1,0);CHECK(!update(0,1,0) && g_mp.device[1]==spare.id);
    mp_campaign_context(MP_CAM_MAP,0,0);CHECK(!update(0,1,0) && g_mp.device[1]==MP_NONE);
    mp_campaign_context(MP_CAM_MAP,1,0);CHECK(!update(0,1,0) && g_mp.device[1]==shared.id);
    mp_campaign_clear();mp_reset(&g_mp);detach_pad(&shared);detach_pad(&spare);events();
    memcpy(g_ram,ram,RAM_SIZE);free(ram);
    g_os=prior_os;g_sdl_mode=prior_sdl;g_lineage=prior_lineage;
    puts("PASS: shared four-player SDL turns/monster scenes need no new Start; inactive loss, held reconnect, group recovery, duel conflict, independent inputs and original choice after loot");
}
static void reconnect_group_tests(void) {
    uint8_t *ram=malloc(RAM_SIZE);CHECK(ram);memcpy(ram,g_ram,RAM_SIZE);
    int prior_os=g_os,prior_sdl=g_sdl_mode,prior_lineage=g_lineage;
    g_os=g_sdl_mode=1;g_lineage=LIN_CRACKED;g_mp_practice=0;
    w16(0x22fc4,0x0c28);w32(0x22fc6,0x0001000b);w16(0x3051e,3);
    for(int players=3;players<=4;players++) {
        w16(0x2e024,players);
        VirtualPad pads[2]={attach_pad(),attach_pad()};events();
        mp_begin_campaign(&g_mp,players,1,1);
        g_mp_campaign=1;g_mp_context=MP_CAM_SELECT;g_mp_ui_player=0;
        for(int p=0;p<players;p++) {
            VirtualPad pad=pads[p<2?0:1];
            g_mp_ui_player=p;g_mp.required=1u<<p;mp_next(&g_mp);
            button(pad,SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
            button(pad,SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
        }
        for(int round=0;round<6;round++) {
            int group=round%2?0:1,active=group?players-1:round%players%2;
            int missing=pads[group].id;
            mp_campaign_context(MP_CAM_MAP,active,0);CHECK(!update(0,1,0));
            detach_pad(&pads[group]);CHECK(update(0,1,0));
            CHECK(g_mp.phase==MP_CLAIM && g_mp.claim==active);
            MpSession before=g_mp;char text[64],expected[64];
            snprintf(expected,sizeof(expected),"PLAYER %d CONNECT CONTROLLER",active+1);
            /* The exact reported mistake: Start on the other group's pad. */
            button(pads[1-group],SDL_CONTROLLER_BUTTON_START,1);
            CHECK(update(0,1,0) && !memcmp(&before,&g_mp,sizeof(g_mp)));
            button(pads[1-group],SDL_CONTROLLER_BUTTON_START,0);CHECK(update(0,1,0));
            CHECK(!strcmp(mp_prompt(1,text),expected));
            /* The same checks after a warm F9-style campaign reconstruction. */
            g_mp_restore_pending=1;mp_campaign_restore();
            mp_campaign_context(MP_CAM_MAP,active,0);
            CHECK(g_mp.phase==MP_CLAIM && !memcmp(before.chosen,g_mp.chosen,sizeof(g_mp.chosen)));
            CHECK(update(1,1,0) && !mp_keyboard_claimable());
            pads[group]=attach_pad();button(pads[group],SDL_CONTROLLER_BUTTON_START,1);
            CHECK(update(0,1,0) && g_mp.device[active]==MP_NONE); /* held at connect */
            button(pads[group],SDL_CONTROLLER_BUTTON_START,0);CHECK(update(0,1,0));
            button(pads[group],SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
            button(pads[group],SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
            CHECK(pads[group].id!=missing && !g_mp.disconnected && g_primary_pad==pads[0].id);
            for(int p=0;p<players;p++) {
                CHECK(g_mp.chosen[p]==pads[p<2?0:1].id);
                mp_campaign_context(MP_CAM_MAP,p,0);CHECK(!update(0,1,0));
                CHECK(g_mp.device[p]==pads[p<2?0:1].id && !mp_prompt(1,text));
                button(pads[0],SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
                button(pads[1],SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);CHECK(!update(0,1,0));
                CHECK(g_mp_input[p]==(p<2?2:1));
                button(pads[0],SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);
                button(pads[1],SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);CHECK(!update(0,1,0));
                mp_campaign_context(MP_CAM_COMBAT,p,1u<<p);CHECK(!update(0,1,0));
                CHECK(g_mp.device[p]==pads[p<2?0:1].id);
                mp_campaign_context(MP_CAM_UI,p,0);CHECK(!update(0,1,0));
            }
            mp_campaign_context(MP_CAM_MAP,-1,0);CHECK(!update(0,1,0));
            mp_campaign_context(MP_CAM_UI,0,0);CHECK(!update(0,1,0));
            CHECK(g_mp.device[0]==pads[0].id); /* original daybreak owner */
        }
        mp_campaign_clear();mp_reset(&g_mp);detach_pad(&pads[0]);detach_pad(&pads[1]);events();
    }
    memcpy(g_ram,ram,RAM_SIZE);free(ram);
    g_os=prior_os;g_sdl_mode=prior_sdl;g_lineage=prior_lineage;
    puts("PASS: three/four-player repeated SDL disconnect/reconnect cannot merge groups; wrong-pad/Enter/held Start rejected, warm recovery, every turn/fight/UI/daybreak and P1 menu ownership preserved");
}
static void duel_keyboard_tests(void) {
    uint8_t *ram=malloc(RAM_SIZE);CHECK(ram);memcpy(ram,g_ram,RAM_SIZE);
    int prior_os=g_os,prior_sdl=g_sdl_mode,prior_lineage=g_lineage;
    g_os=g_sdl_mode=1;g_lineage=LIN_CRACKED;g_mp_practice=0;
    w16(0x22fc4,0x0c28);w32(0x22fc6,0x0001000b);w16(0x3051e,3);w16(0x2e024,3);
    VirtualPad shared=attach_pad(),spare=attach_pad();events();
    mp_begin_campaign(&g_mp,3,1,1);
    g_mp_campaign=1;g_mp_context=MP_CAM_SELECT;g_mp_ui_player=0;
    for(int p=0;p<3;p++) {
        VirtualPad pad=p==2?spare:shared;
        g_mp_ui_player=p;g_mp.required=1u<<p;mp_next(&g_mp);
        button(pad,SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
        button(pad,SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
    }
    int chosen[4];memcpy(chosen,g_mp.chosen,sizeof(chosen));
    mp_campaign_context(MP_CAM_MAP,0,0);CHECK(!update(0,1,0));
    mp_campaign_context(MP_CAM_COMBAT,0,3);
    char text[64];
    CHECK(update(0,1,0) && g_mp.claim==1 && g_mp.device[1]==MP_NONE);
    CHECK(!strcmp(mp_prompt(1,text),"PLAYER 2 PRESS START OR ENTER"));
    /* Neither availability nor a held/background Enter assigns the keyboard. */
    for(int i=0;i<10;i++) CHECK(update(0,1,0) && g_mp.device[1]==MP_NONE);
    probe_keys[SDL_SCANCODE_RETURN]=1;
    CHECK(update(1,0,0) && g_mp.device[1]==MP_NONE);
    CHECK(update(0,1,0) && g_mp.device[1]==MP_NONE);
    probe_keys[SDL_SCANCODE_RETURN]=0;CHECK(update(0,1,0));
    probe_keys[SDL_SCANCODE_RETURN]=1;
    CHECK(update(1,1,0) && g_mp.phase==MP_RELEASE && !mp_prompt(1,text));
    CHECK(g_mp.device[1]==MP_KEYBOARD && !memcmp(chosen,g_mp.chosen,sizeof(chosen)));
    button(shared,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);probe_keys[SDL_SCANCODE_RIGHT]=1;
    CHECK(update(0,1,0) && !g_mp_input[0] && !g_mp_input[1]);
    probe_keys[SDL_SCANCODE_RETURN]=0;
    CHECK(!update(0,1,0) && g_mp_input[0]==2 && g_mp_input[1]==1 && !g_mp_input[2]);
    CHECK(g_primary_pad==shared.id && !g_pause_request);
    button(shared,SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);memset(probe_keys,0,sizeof(probe_keys));
    mp_campaign_context(MP_CAM_UI,1,0);
    probe_keys[SDL_SCANCODE_LEFT]=1;button(shared,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    CHECK(!update(0,1,0) && g_mp.device[1]==MP_KEYBOARD && g_mp_input[1]==2 && !g_mp_input[0]);
    CHECK(g_ji_lf && !g_ji_rt && mp_keyboard_gameplay());
    button(shared,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);memset(probe_keys,0,sizeof(probe_keys));
    /* An AI map turn must end the loan before an immediate AI attack too. */
    mp_campaign_context(MP_CAM_MAP,-1,0);
    CHECK(!update(0,1,0) && mp_owner(&g_mp,MP_KEYBOARD)<0);
    mp_campaign_context(MP_CAM_COMBAT,1,2);
    CHECK(!update(0,1,0) && g_mp.device[1]==shared.id && !mp_prompt(1,text));
    /* Borrow again in a later human duel, this time using Numpad Enter. */
    mp_campaign_context(MP_CAM_MAP,0,0);CHECK(!update(0,1,0));
    mp_campaign_context(MP_CAM_COMBAT,0,3);CHECK(update(0,1,0));
    probe_keys[SDL_SCANCODE_KP_ENTER]=1;CHECK(update(1,1,0));
    probe_keys[SDL_SCANCODE_KP_ENTER]=0;CHECK(!update(0,1,0));
    CHECK(g_mp.device[1]==MP_KEYBOARD && !memcmp(chosen,g_mp.chosen,sizeof(chosen)));
    mp_campaign_context(MP_CAM_UI,1,0);CHECK(!update(0,1,0));
    /* Losing the usual pad during keyboard loot does not interrupt that loot.
     * Returning to its normal turn still requires an explicit controller claim. */
    detach_pad(&shared);CHECK(!update(0,1,0) && g_mp.disconnected==3);
    mp_campaign_context(MP_CAM_MAP,2,0);CHECK(!update(0,1,0));
    CHECK(g_mp.device[2]==spare.id && mp_owner(&g_mp,MP_KEYBOARD)<0);
    mp_campaign_context(MP_CAM_MAP,1,0);
    CHECK(update(1,1,0) && g_mp.claim==1 && !mp_keyboard_claimable());
    shared=attach_pad();events();CHECK(update(0,1,0) && g_mp.device[1]==MP_NONE);
    button(shared,SDL_CONTROLLER_BUTTON_START,1);CHECK(update(0,1,0));
    button(shared,SDL_CONTROLLER_BUTTON_START,0);CHECK(!update(0,1,0));
    CHECK(g_mp.chosen[0]==shared.id && g_mp.chosen[1]==shared.id && g_mp.chosen[2]==spare.id);
    CHECK(!g_mp.disconnected && g_primary_pad==shared.id);
    mp_campaign_context(MP_CAM_MAP,0,0);CHECK(!update(0,1,0));
    CHECK(g_mp.device[0]==shared.id && mp_owner(&g_mp,MP_KEYBOARD)<0);
    mp_campaign_context(MP_CAM_COMBAT,0,3);CHECK(update(0,1,0) && mp_keyboard_claimable());
    /* Even during a duel, a lost normal controller cannot be replaced by Enter. */
    detach_pad(&shared);CHECK(update(1,1,0) && !mp_keyboard_claimable());
    CHECK(mp_owner(&g_mp,MP_KEYBOARD)<0 && g_mp.disconnected==3);
    mp_campaign_clear();mp_reset(&g_mp);detach_pad(&spare);events();
    memcpy(g_ram,ram,RAM_SIZE);free(ram);
    g_os=prior_os;g_sdl_mode=prior_sdl;g_lineage=prior_lineage;

    MpSession s;mp_begin_campaign(&s,3,1,1);CHECK(mp_claim(&s,10));
    s.required=2;mp_next(&s);CHECK(mp_claim(&s,10));
    s.required=4;mp_next(&s);CHECK(mp_claim(&s,MP_KEYBOARD));
    s.required=1;mp_use_choices(&s,0,0);s.required=3;mp_use_choices(&s,0,1);
    /* The remembered keyboard reservation counts even without an active binding. */
    s.device[2]=MP_NONE;CHECK(!mp_can_claim(&s,MP_KEYBOARD));
    mp_begin_campaign(&s,2,0,3);CHECK(mp_claim(&s,10));CHECK(mp_claim(&s,20));
    mp_removed(&s,20);CHECK(!mp_claim(&s,MP_KEYBOARD));
    puts("PASS: explicit unused keyboard for shared-pad duel, visible Start/Enter choice, input isolation, keyboard loot, human/AI map cleanup, reserved keyboard and no disconnect fallback");
}
static void unit_tests(void) {
    SDL_SetMainReady();
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER) == 0);
    controls_set_defaults();
    menu_device_tests();
    initial_assignment_tests();
    confirmation_device_tests();
    mixed_campaign_device_tests();
    scene_input_tests();
    shared_campaign_tests();
    reconnect_group_tests();
    duel_keyboard_tests();
    g_mp_practice=1;
    mp_begin(&g_mp);
    CHECK(update(0,1,0) && g_mp.claim == 0 && !mp_keyboard_claimable());
    probe_a=attach_pad();
    button(probe_a,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,1,0) && g_mp.device[0] == MP_NONE); /* held when connected */
    button(probe_a,SDL_CONTROLLER_BUTTON_START,0); events();
    CHECK(mp_pad_count() == 1);
    controller_open_all(&probe_notice);
    events(); CHECK(mp_pad_count() == 1 && !controller_primary(1));
    button(probe_a,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,0,0) && g_mp.device[0] == MP_NONE); /* no background claims */
    button(probe_a,SDL_CONTROLLER_BUTTON_START,0); update(0,1,0);
    button(probe_a,SDL_CONTROLLER_BUTTON_START,1);
    CHECK(update(0,1,0) && g_mp.phase == MP_RELEASE);
    CHECK(g_mp.device[0] == probe_a.id && g_mp.device[1] == MP_KEYBOARD);
    CHECK(g_mp_input[0] == 0 && g_pause_request == 0);
    button(probe_a,SDL_CONTROLLER_BUTTON_START,0);
    axis(probe_a,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);
    CHECK(!update(0,1,0) && g_mp_input[0]==16);
    axis(probe_a,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);
    CHECK(!update(0,1,0));
    button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
    probe_keys[SDL_SCANCODE_RIGHT]=1;
    CHECK(!update(0,1,1) && g_mp_input[0] == 2 && g_mp_input[1] == 17);
    button(probe_a,SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);
    memset(probe_keys,0,sizeof(probe_keys)); update(0,1,0);
    probe_b=attach_pad();
    CHECK(!update(0,1,0) && g_mp.phase==MP_PLAY && g_mp.device[0]==probe_a.id && g_mp.device[1]==MP_KEYBOARD);
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1); CHECK(!update(0,1,0));
    button(probe_b,SDL_CONTROLLER_BUTTON_START,0); CHECK(!update(0,1,0));
    CHECK(g_mp.device[1]==MP_KEYBOARD && !g_pause_request); /* no mid-session offer/takeover */
    mp_begin_practice(); /* next Practice session can use the newly connected pad */
    CHECK(!update(0,1,0) && g_mp.phase==MP_PLAY);
    CHECK(g_mp.device[0] == probe_a.id && g_mp.device[1] == probe_b.id);
    probe_keys[SDL_SCANCODE_UP]=1;
    button(probe_b,SDL_CONTROLLER_BUTTON_A,1);
    CHECK(!update(0,1,1) && g_mp_input[0] == 0 && g_mp_input[1] == 16);
    memset(probe_keys,0,sizeof(probe_keys)); button(probe_b,SDL_CONTROLLER_BUTTON_A,0);
    VirtualPad extra=attach_pad(); button(extra,SDL_CONTROLLER_BUTTON_A,1);
    CHECK(!update(0,1,0) && !g_mp_input[0] && !g_mp_input[1]);
    detach_pad(&extra); CHECK(!update(0,1,0));
    detach_pad(&probe_a);
    CHECK(update(0,1,0) && g_mp.claim == 0 && g_mp.device[1] == probe_b.id);
    probe_keys[SDL_SCANCODE_RETURN]=1;
    CHECK(update(1,1,0) && g_mp.device[0] == MP_NONE && g_mp.device[1] == probe_b.id);
    CHECK(!mp_keyboard_claimable() && !mp_can_claim(&g_mp,MP_KEYBOARD));
    memset(probe_keys,0,sizeof(probe_keys)); CHECK(update(0,1,0));
    probe_a=attach_pad(); CHECK(update(0,1,0) && g_mp.claim == 0);
    button(probe_a,SDL_CONTROLLER_BUTTON_START,1); CHECK(update(0,1,0));
    button(probe_a,SDL_CONTROLLER_BUTTON_START,0); CHECK(!update(0,1,0));
    detach_pad(&probe_b); CHECK(update(0,1,0) && g_mp.claim == 1 && g_mp.device[0] == probe_a.id);
    detach_pad(&probe_a); CHECK(update(0,1,0) && !mp_keyboard_claimable());
    CHECK(g_mp.device[0] == MP_NONE && g_mp.device[1] == MP_NONE && !mp_pad_count());
    probe_a=attach_pad(); events();
    mp_begin(&g_mp);
    probe_b=attach_pad(); events();
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1); CHECK(update(0,1,0));
    CHECK(g_mp.device[0] == probe_b.id && g_mp.device[1] == probe_a.id);
    button(probe_b,SDL_CONTROLLER_BUTTON_START,0); CHECK(!update(0,1,0));
    CHECK(g_mp.device[0] == probe_b.id && g_mp.device[1] == probe_a.id);
    CHECK(g_primary_pad == probe_b.id); /* explicit P1 replaces the former menu owner */
    mp_next(&g_mp); probe_keys[SDL_SCANCODE_LCTRL]=1;
    CHECK(!update(0,1,0)); memset(probe_keys,0,sizeof(probe_keys)); CHECK(!update(0,1,0));
    CHECK(g_mp.device[0] == probe_b.id && g_mp.device[1] == probe_a.id);
    button(probe_b,SDL_CONTROLLER_BUTTON_START,1); CHECK(!update(0,1,0));
    g_mp_practice=0; mp_reset(&g_mp); events();
    CHECK(controller_primary(1)->id == probe_b.id);
    CHECK(controller_primary(1)->now[CONTROL_PAUSE] && !controller_primary(1)->edge[CONTROL_PAUSE]);
    detach_pad(&probe_a); detach_pad(&probe_b); events(); mp_close_pads(); SDL_Quit();
    puts("PASS: zero/one/two/extra identical pads, reversed claims, input isolation, held triggers, controller-only recovery, both disconnect orders, all-device loss; P1 retains menu ownership without replaying held shortcuts");
}
int main(int argc, char **argv) {
    const char *log=NULL;
    int replay=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--log") && i+1<argc) log=argv[++i];
        else if (!strcmp(argv[i],"--probe-live")) probe_live=1;
        else if (!strcmp(argv[i],"--probe-menu")) probe_live=2;
        else if (!strcmp(argv[i],"--probe-prompts")) probe_live=3;
        else if (!strcmp(argv[i],"--probe-solo")) probe_live=4;
        else if (!strcmp(argv[i],"--probe-entry") && i+1<argc) {
            probe_live=5; probe_entry_players=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-entry-devices") && i+2<argc) {
            probe_entry_pad_count=atoi(argv[++i]);probe_entry_owner=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-menu-fixture") && i+1<argc) probe_menu_fixture=argv[++i];
        else if (!strcmp(argv[i],"--probe-campaign")) probe_live=6;
        else if (!strcmp(argv[i],"--probe-campaign-setup") && i+2<argc) {
            probe_live=7;campaign_players=atoi(argv[++i]);campaign_pad_count=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-campaign-duel") && i+3<argc) {
            probe_live=8;campaign_players=atoi(argv[++i]);campaign_pad_count=atoi(argv[++i]);
            campaign_reverse=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-campaign-restore") && i+3<argc) {
            probe_live=9;campaign_players=atoi(argv[++i]);campaign_pad_count=atoi(argv[++i]);
            campaign_expected_context=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-campaign-save") && i+1<argc) campaign_save=argv[++i];
        else if (!strcmp(argv[i],"--probe-keyboard-player") && i+1<argc) campaign_keyboard_player=atoi(argv[++i])-1;
        else if (!strcmp(argv[i],"--probe-zone-after-setup") && i+2<argc) {
            campaign_zone_owner=atoi(argv[++i])-1;campaign_zone_node=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-zone-before")) campaign_zone_before=1;
        else if (!strcmp(argv[i],"--probe-duel-opponent") && i+1<argc) campaign_duel_opponent=atoi(argv[++i])-1;
        else if (!strcmp(argv[i],"--probe-duel-keyboard")) campaign_duel_keyboard=1;
        else if (!strcmp(argv[i],"--probe-duel-keyboard-before")) campaign_duel_keyboard=campaign_duel_keyboard_before=1;
        else if (!strcmp(argv[i],"--probe-duel-after-setup") && i+1<argc) {
            campaign_duel_after_setup=1;campaign_reverse=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"--probe-replay")) replay=1;
        else if (!strcmp(argv[i],"--probe-image") && i+1<argc) probe_image=argv[++i];
    }
    if (!log) return 2;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if (replay) return moonstone_main(argc,argv);
    if (probe_live) {
        if(probe_live>=7) {
            CHECK(campaign_players>=2 && campaign_players<=4 && campaign_pad_count>=1 && campaign_pad_count<=4);
            SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
            for(int p=0;p<campaign_pad_count;p++) campaign_pads[p]=attach_pad();
        }
        if(probe_live==5) {
            CHECK(probe_entry_pad_count>=1 && probe_entry_pad_count<=4 && probe_entry_owner>=0 && probe_entry_owner<probe_entry_pad_count);
            SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
            for(int i=0;i<probe_entry_pad_count;i++)probe_entry_pads[i]=attach_pad();
        }
        if (probe_live == 2 || probe_live == 3 || probe_live == 6) {
            SDL_SetMainReady();
            CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
            probe_a=attach_pad(); probe_b=attach_pad();
        }
        int rc=moonstone_main(argc,argv);
        if (probe_live>=7) {
            CHECK(campaign_step==4);
            if(campaign_duel_after_setup) CHECK(campaign_scene_checks>0);
            printf("PASS: %d assigned campaign scene transitions run without a release gate or prompt\n",campaign_scene_checks);
            return rc;
        }
        CHECK(probe_frame >= (probe_live == 2 ? 27 : probe_live == 3 ? 52 : probe_live == 4 ? 44 : probe_live == 5 ? 128 : probe_live == 6 ? 24 : 74));
        if (probe_live == 5) CHECK(probe_entry_players == 0);
        return rc;
    }
    g_log=fopen(log,"w"); CHECK(g_log); unit_tests(); fclose(g_log); return 0;
}
