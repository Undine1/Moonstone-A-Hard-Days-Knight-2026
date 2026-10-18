/* Test the real SDL boot image upload and fullscreen rendering. Never ship. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *output;
static SDL_Window *window;
static int presents, uploads;
static SDL_Window *probe_window(const char *, int, int, int, int, Uint32);
static int probe_update(SDL_Texture *, const SDL_Rect *, const void *, int);
static void probe_present(SDL_Renderer *);
static SDL_bool probe_controller(int i) { (void)i; return SDL_FALSE; }
#define SDL_CreateWindow probe_window
#define SDL_UpdateTexture probe_update
#define SDL_RenderPresent probe_present
#define SDL_IsGameController probe_controller
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef SDL_IsGameController
#undef SDL_RenderPresent
#undef SDL_UpdateTexture
#undef SDL_CreateWindow
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL splash line %d: %s (%s)\n",__LINE__,#x,SDL_GetError()); exit(2); } } while (0)
static SDL_Window *probe_window(const char *t,int x,int y,int w,int h,Uint32 flags) {
    window=SDL_CreateWindow(t,x,y,w,h,flags); return window;
}
static int probe_update(SDL_Texture *t,const SDL_Rect *r,const void *pixels,int pitch) {
    int w,h; CHECK(SDL_QueryTexture(t,NULL,NULL,&w,&h)==0);
    if (w==SPLASH_W && h==SPLASH_H) {
        char path[1200]; snprintf(path,sizeof(path),"%s/splash.rgb",output);
        FILE *file=fopen(path,"wb"); CHECK(file && !r && pitch>=w*3);
        for (int y=0;y<h;y++) CHECK(fwrite((const Uint8 *)pixels+y*pitch,1,(size_t)w*3,file)==(size_t)w*3);
        CHECK(fclose(file)==0); uploads++;
    }
    return SDL_UpdateTexture(t,r,pixels,pitch);
}
static void picture(SDL_Renderer *ren,const char *name) {
    int w,h,lw,lh; CHECK(SDL_GetRendererOutputSize(ren,&w,&h)==0);
    SDL_RenderGetLogicalSize(ren,&lw,&lh); CHECK(SDL_RenderSetLogicalSize(ren,0,0)==0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(s && SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    char path[1200]; snprintf(path,sizeof(path),"%s/%s.bmp",output,name);
    CHECK(SDL_SaveBMP(s,path)==0); SDL_FreeSurface(s);
    CHECK(SDL_RenderSetLogicalSize(ren,lw,lh)==0);
}
static void toggle(void) {
    SDL_Event e={0}; e.type=SDL_KEYDOWN; e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.scancode=SDL_SCANCODE_F11; e.key.keysym.sym=SDLK_F11;
    CHECK(SDL_PushEvent(&e)==1); e.type=SDL_KEYUP; CHECK(SDL_PushEvent(&e)==1);
}
static void probe_present(SDL_Renderer *ren) {
    int n=++presents;
    if (n==1) { CHECK(uploads==1); picture(ren,"windowed"); }
    if (n==2 || n==8) toggle();
    if (n==5) {
        CHECK(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN);
        picture(ren,"fullscreen");
    }
    if (n==11) {
        CHECK(!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));
        picture(ren,"restored");
    }
    if (n==12) { SDL_Event e={0}; e.type=SDL_QUIT; CHECK(SDL_PushEvent(&e)==1); }
    SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int has_log=0;
    char **args=calloc((size_t)argc+1,sizeof(char *)); CHECK(args);
    int count=0;
    for (int i=0;i<argc;i++) {
        if (!strcmp(argv[i],"--probe-output") && i+1<argc) output=argv[++i];
        else { if (!strcmp(argv[i],"--log") && i+1<argc) has_log=1; args[count++]=argv[i]; }
    }
    CHECK(has_log && output);
    int rc=moonstone_main(count,args); free(args);
    CHECK(!rc && uploads==1 && presents>=12 && presents<=13);
    puts("PASS real SDL splash upload, fullscreen and window restoration");
    return rc;
}
