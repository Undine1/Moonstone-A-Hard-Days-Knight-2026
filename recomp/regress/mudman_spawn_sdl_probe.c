/* Actual SDL window/fullscreen rendering of a private Mudman spawn save. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
static SDL_Window *make_window(const char*,int,int,int,int,Uint32);
static void present(SDL_Renderer*);
static Uint8 keys[SDL_NUM_SCANCODES];
static const Uint8 *probe_keyboard(int *n){if(n)*n=SDL_NUM_SCANCODES;return keys;}
static SDL_bool controllers(int i){(void)i;return SDL_FALSE;}
#define SDL_GetKeyboardState probe_keyboard
#define SDL_IsGameController controllers
#define SDL_CreateWindow make_window
#define SDL_RenderPresent present
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef SDL_GetKeyboardState
#undef SDL_IsGameController
#undef SDL_CreateWindow
#undef SDL_RenderPresent
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL SDL %d: %s\n",__LINE__,#x);exit(86);}}while(0)
static const char *fixture,*output;
static int full,presents;
static SDL_Window *make_window(const char*t,int x,int y,int w,int h,Uint32 flags){
    g_bootphase=0;g_sdl_mode=0;CHECK(load_state(fixture));g_sdl_mode=1;
    SDL_Window *win=SDL_CreateWindow(t,x,y,w,h,flags);CHECK(win);
    if(full)CHECK(SDL_SetWindowFullscreen(win,SDL_WINDOW_FULLSCREEN_DESKTOP)==0);
    return win;
}
static void present(SDL_Renderer *ren){
    presents++;CHECK(presents<=121);
    char path[1400];snprintf(path,sizeof(path),"%s/frame-%03d.ppm",output,presents);
    FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n%d %d\n255\n",g_cap_w,g_cap_h);
    for(int y=0;y<g_cap_h;y++)CHECK(fwrite(g_cap+(size_t)y*FB_W*3,3,g_cap_w,f)==(size_t)g_cap_w);
    fclose(f);
    if(presents==120){
        snprintf(path,sizeof(path),"%s/final.sav",output);CHECK(save_state(path));
        printf("PASS SDL full=%d profile=%s frames=120\n",full,MOON_DEVELOPER?"dev":"public");
        SDL_Event e={0};e.type=SDL_QUIT;CHECK(SDL_PushEvent(&e)==1);
    }
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv){
    int n=1,log=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--fixture") && i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--output") && i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--full"))full=1;
        else {if(!strcmp(argv[i],"--log"))log=1;argv[n++]=argv[i];}
    }
    CHECK(fixture && output && log);argv[n]=NULL;int rc=moonstone_main(n,argv);CHECK(presents==121);return rc;
}
