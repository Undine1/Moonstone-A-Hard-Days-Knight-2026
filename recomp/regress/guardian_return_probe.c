/* Actual SDL Guardian reward transition. Test executable; never distribute. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static const Uint8 *probe_keyboard(int *n) { if(n)*n=SDL_NUM_SCANCODES;return keys; }
static SDL_bool controllers(int i) { (void)i;return SDL_FALSE; }
static SDL_Window *make_window(const char*,int,int,int,int,Uint32);
static void present(SDL_Renderer*);
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
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL guardian SDL line%d: %s\n",__LINE__,#x);exit(86);} }while(0)
static const char *fixture,*output,*reload_fixture;
static int full,presents,redraws,tweens,reload_phase,reloaded,completed;
static unsigned char guest_before[RAM_SIZE];
static FILE *hashes;

static SDL_Window *make_window(const char*t,int x,int y,int w,int h,Uint32 flags) {
    g_bootphase=0;g_sdl_mode=0;CHECK(load_state(fixture));g_sdl_mode=1;
    SDL_Window *win=SDL_CreateWindow(t,x,y,w,h,flags);CHECK(win);
    if(full)CHECK(SDL_SetWindowFullscreen(win,SDL_WINDOW_FULLSCREEN_DESKTOP)==0);
    return win;
}
void moon_instr_hook(unsigned pc) {
    int old=g_guardian_return_fade;
    /* The new boundary must change presentation state only. */
    int check=old==1 && pc==0x4014e;
    unsigned regs[SAVE_NREGS];
    if(check) {
        memcpy(guest_before,g_ram,RAM_SIZE);
        for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
    }
    production_hook(pc);
    if(check) {
        CHECK(!memcmp(guest_before,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)CHECK(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
    }
    if(old!=g_guardian_return_fade) {
        if(g_guardian_return_fade==3)redraws++;
        if(g_guardian_return_fade==2)tweens++;
    }
}
static void present(SDL_Renderer *ren) {
    presents++;CHECK(presents<=181);
    if(presents==1) {
        SDL_Event e={0};e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(SDL_RenderGetWindow(ren));
        e.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;CHECK(SDL_PushEvent(&e)==1);
    }
    if(presents==10)keys[SDL_SCANCODE_LCTRL]=1;
    if(presents==18)keys[SDL_SCANCODE_LCTRL]=0;
    uint64_t hash=14695981039346656037ull;unsigned ink=0;
    for(int y=0;y<g_cap_h;y++)for(int x=0;x<g_cap_w*3;x++) {
        unsigned char b=g_cap[y*FB_W*3+x];hash^=b;hash*=1099511628211ull;ink+=b!=0;
    }
    fprintf(hashes,"%d %016llx %d %u\n",presents,(unsigned long long)hash,g_guardian_return_fade,ink);
    if(g_guardian_return_fade==3 || g_guardian_return_fade==2)CHECK(!ink);
    if(reload_fixture && !reloaded && g_guardian_return_fade==reload_phase) {
        CHECK(load_state(reload_fixture));reloaded=1;
        CHECK(!g_guardian_return_fade && !g_guardian_fade_seen && g_guardian_fade_deadline==-1);
    }
    if(presents==50 || presents==53 || presents==90) {
        char path[1400];snprintf(path,sizeof(path),"%s/frame-%d.ppm",output,presents);
        FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n%d %d\n255\n",g_cap_w,g_cap_h);
        for(int y=0;y<g_cap_h;y++)CHECK(fwrite(g_cap+y*FB_W*3,3,g_cap_w,f)==(unsigned)g_cap_w);
        fclose(f);
    }
    if(presents==180) {
        CHECK(redraws==1 && (!reload_fixture || reloaded));
        CHECK(!reload_fixture ? (tweens==1 && !g_guardian_return_fade && ink>1000) : !g_guardian_return_fade);
        char path[1400];snprintf(path,sizeof(path),"%s/final.sav",output);CHECK(save_state(path));
        printf("PASS guardian SDL profile=%s full=%d redraw=%d tween=%d reload=%d\n",MOON_DEVELOPER?"dev":"public",full,redraws,tweens,reloaded);
        completed=1;SDL_Event e={0};e.type=SDL_QUIT;CHECK(SDL_PushEvent(&e)==1);
    }
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int n=1,log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--fixture")&&i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--output")&&i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--full"))full=1;
        else if(!strcmp(argv[i],"--reload")&&i+2<argc){reload_phase=atoi(argv[++i]);reload_fixture=argv[++i];}
        else {if(!strcmp(argv[i],"--log"))log=1;argv[n++]=argv[i];}
    }
    CHECK(fixture&&output&&log);argv[n]=NULL;
    char path[1400];snprintf(path,sizeof(path),"%s/frames.txt",output);hashes=fopen(path,"w");CHECK(hashes);
    int rc=moonstone_main(n,argv);fclose(hashes);CHECK(completed);return rc;
}
