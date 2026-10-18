/* Actual SDL window/fullscreen recovery of the saved spear execution.
 * Private diagnostic only; never deploy or package this executable. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
static SDL_Window *make_window(const char*,int,int,int,int,Uint32);
static void present(SDL_Renderer*);
static Uint8 keys[SDL_NUM_SCANCODES];
static const Uint8 *probe_keyboard(int*n){if(n)*n=SDL_NUM_SCANCODES;return keys;}
static SDL_bool controllers(int i){(void)i;return SDL_FALSE;}
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
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL SDL line%d %s\n",__LINE__,#x);exit(2);}}while(0)
static const char *fixture,*output;
static int full,presents,recovered,executed,lives;
static uint64_t pixels=14695981039346656037ull;
static SDL_Window *make_window(const char*t,int x,int y,int w,int h,Uint32 flags){
    g_bootphase=0;g_sdl_mode=0;CHECK(load_state(fixture));g_sdl_mode=1;
    SDL_Window *win=SDL_CreateWindow(t,x,y,w,h,flags);CHECK(win);
    if(full)CHECK(SDL_SetWindowFullscreen(win,SDL_WINDOW_FULLSCREEN_DESKTOP)==0);
    return win;
}
void moon_instr_hook(unsigned pc){
    if(pc==0x28a7c){
        unsigned d=m68k_get_reg(NULL,M68K_REG_A1);
        if(d>=0x2eca0 && d<0x2eca0+500 && (d-0x2eca0)%50==0
           && r32(d+0x18)==0x13460e && r32(d+2)==0x32b90)executed++;
    }
    if(pc==0x21434)lives++;
    unsigned p=pc==0x288c8?r32(0x2edfe + 2):0;
    production_hook(pc);
    if(p && p!=r32(0x2edfe + 2))recovered++;
}
static void present(SDL_Renderer *ren){
    presents++;CHECK(presents<=601);
    for(int y=0;y<g_cap_h;y++)for(int x=0;x<g_cap_w*3;x++){
        pixels^=g_cap[y*FB_W*3+x];pixels*=1099511628211ull;
    }
    if(presents==15 || presents==50 || presents==100 || presents==600){
        char path[1400];snprintf(path,sizeof(path),"%s/frame-%d.ppm",output,presents);
        FILE*f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n%d %d\n255\n",g_cap_w,g_cap_h);
        for(int y=0;y<g_cap_h;y++)CHECK(fwrite(g_cap+(size_t)y*FB_W*3,3,g_cap_w,f)==(size_t)g_cap_w);
        fclose(f);
    }
    if(presents==600){
        CHECK(recovered==1 && executed==1 && lives==1);
        CHECK(r8(0x2e7dc+0x49)==0 && r16(0x2e7dc+0x50)==90 && !r8(0x2e0bc+8));
        char path[1400];snprintf(path,sizeof(path),"%s/final.sav",output);CHECK(save_state(path));
        printf("PASS SDL full=%d profile=%s recovered=1 execution=1 life-loss=1 pixels=%016llx\n",
               full,MOON_DEVELOPER?"dev":"public",(unsigned long long)pixels);
        SDL_Event e={0};e.type=SDL_QUIT;CHECK(SDL_PushEvent(&e)==1);
    }
    SDL_RenderPresent(ren);
}
int main(int argc,char**argv){
    int n=1,log=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--fixture")&&i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--output")&&i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--full"))full=1;
        else{if(!strcmp(argv[i],"--log"))log=1;argv[n++]=argv[i];}
    }
    CHECK(fixture&&output&&log);argv[n]=NULL;int rc=moonstone_main(n,argv);CHECK(presents==601);return rc;
}
