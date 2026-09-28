/* Test-only SDL F5/F9 and profile lifetime checks. Never distribute this EXE. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
static Uint8 keys[SDL_NUM_SCANCODES];
static int presents, saved, loaded, failed;
static int saved_marker=0x42;
static const char *mode, *expected, *other_fixture, *initial_fixture, *result_path;
static const Uint8 *profile_keyboard(int *n) { if(n) *n=SDL_NUM_SCANCODES; return keys; }
static SDL_bool no_controllers(int index) { (void)index; return SDL_FALSE; }
static SDL_Window *hidden_window(const char *t,int x,int y,int w,int h,Uint32 f) {
    return SDL_CreateWindow(t,x,y,w,h,f|SDL_WINDOW_HIDDEN);
}
static void present(SDL_Renderer *ren);
static void title(SDL_Window *win,const char *text);
#define SDL_GetKeyboardState profile_keyboard
#define SDL_IsGameController no_controllers
#define SDL_CreateWindow hidden_window
#define SDL_RenderPresent present
#define SDL_SetWindowTitle title
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef SDL_SetWindowTitle
#undef SDL_RenderPresent
#undef SDL_CreateWindow
#undef SDL_IsGameController
#undef SDL_GetKeyboardState
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL frame %d line %d: %s\n",presents,__LINE__,#x); exit(2); } } while(0)
#ifdef _WIN32
static HANDLE locked = INVALID_HANDLE_VALUE;
#endif
static void key(SDL_Scancode scan,int down) {
    keys[scan]=(Uint8)down;
    SDL_Event e={0}; e.type=down?SDL_KEYDOWN:SDL_KEYUP;
    e.key.keysym.scancode=scan; e.key.keysym.sym=SDL_GetKeyFromScancode(scan);
    CHECK(SDL_PushEvent(&e)==1);
}
static void title(SDL_Window *win,const char *text) {
    CHECK(!strcmp(save_profile_name(),expected));
    CHECK(strstr(text,expected));
    if(strstr(text,"<<< GAME SAVED >>>")) saved++;
    if(strstr(text,"<<< GAME LOADED >>>")) { CHECK(g_ram[RAM_SIZE-1]==saved_marker); loaded++; }
    if(strstr(text,"<<< NO SAVE TO LOAD >>>")) { CHECK(g_ram[RAM_SIZE-1]==0x99); failed++; }
    if(strstr(text,"<<< SAVE FAILED >>>")) failed++;
    SDL_SetWindowTitle(win,text);
}
static void present(SDL_Renderer *ren) {
    int n=++presents;
    CHECK(!strcmp(save_profile_name(),expected));
    if(n==1) {
        CHECK(strstr(g_wintitle,expected)); CHECK(!g_blt_busy_scope);
        if(!strcmp(mode,"renamed")) {
            char path[1100]; quicksave_path(path,sizeof(path));
            FILE *in=fopen(path,"rb"); CHECK(in);
            CHECK(fseek(in,20+4*SAVE_NREGS+RAM_SIZE-1,SEEK_SET)==0);
            saved_marker=fgetc(in); CHECK(saved_marker>=0); CHECK(fclose(in)==0);
        }
        if(!strcmp(mode,"write-failure")) {
#ifdef _WIN32
            char path[1100]; quicksave_path(path,sizeof(path));
            /* Existing save can be read, but cannot be replaced while locked. */
            locked=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
            CHECK(locked!=INVALID_HANDLE_VALUE);
#else
            CHECK(0);
#endif
        }
    }
    if(n==5) {
        g_ram[RAM_SIZE-1]=0x42;
        if(!strcmp(mode,"roundtrip") || !strcmp(mode,"write-failure") || !strcmp(mode,"folder-failure")) key(SDL_SCANCODE_F5,1);
    }
    if(n==7) key(SDL_SCANCODE_F5,0);
    if(n==10 && strcmp(mode,"write-failure") && strcmp(mode,"folder-failure")) {
        g_ram[RAM_SIZE-1]=0x99; key(SDL_SCANCODE_F9,1);
    }
    if(n==12) key(SDL_SCANCODE_F9,0);
    if(n==15 && other_fixture) {
        char before[1100],after[1100]; quicksave_path(before,sizeof(before));
        CHECK(load_state(other_fixture));
        quicksave_path(after,sizeof(after)); CHECK(!strcmp(before,after));
        CHECK(!strcmp(save_profile_name(),expected));
        CHECK(load_state(initial_fixture));
    }
    if(n==18 && !strcmp(mode,"roundtrip")) { g_ram[RAM_SIZE-1]=0x42; key(SDL_SCANCODE_F5,1); }
    if(n==20) key(SDL_SCANCODE_F5,0);
    if(n==25) {
        if(!strcmp(mode,"roundtrip")) CHECK(saved==2 && loaded==1 && !failed);
        else if(!strcmp(mode,"renamed")) CHECK(!saved && loaded==1 && !failed);
        else CHECK(!saved && !loaded && failed==1);
#ifdef _WIN32
        if(locked!=INVALID_HANDLE_VALUE) { CloseHandle(locked); locked=INVALID_HANDLE_VALUE; }
#endif
        SDL_Event e={.type=SDL_QUIT}; CHECK(SDL_PushEvent(&e)==1);
    }
    CHECK(n<=26); SDL_RenderPresent(ren);
}
int main(int argc,char **argv) {
    int log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--log") && i+1<argc) { log=1; i++; }
        else if(!strcmp(argv[i],"--probe-mode") && i+1<argc) mode=argv[++i];
        else if(!strcmp(argv[i],"--probe-profile") && i+1<argc) expected=argv[++i];
        else if(!strcmp(argv[i],"--probe-other") && i+1<argc) other_fixture=argv[++i];
        else if(!strcmp(argv[i],"--loadstate") && i+1<argc) initial_fixture=argv[++i];
        else if(!strcmp(argv[i],"--probe-result") && i+1<argc) result_path=argv[++i];
    }
    if(!log || !mode || !expected || !initial_fixture || !result_path) return 2;
    int rc=moonstone_main(argc,argv);
    CHECK(rc==0 && presents==26);
    FILE *out=fopen(result_path,"w"); CHECK(out);
    fprintf(out,"PASS %s %s: saves=%d loads=%d failures=%d\n",expected,mode,saved,loaded,failed);
    CHECK(fclose(out)==0); return 0;
}
