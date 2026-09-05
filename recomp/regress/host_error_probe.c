/* Test-only host failure injection. Build separately; never package this exe.
 * Includes the actual runtime so warning paths and log routing stay identical. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

static SDL_AudioDeviceID probe_audio_open(const char *device, int capture,
                                         const SDL_AudioSpec *want, SDL_AudioSpec *have, int changes) {
    (void)device; (void)capture; (void)want; (void)have; (void)changes;
    SDL_Event quit = {0};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit); /* exit the live loop cleanly after the warning */
    SDL_SetError("Injected audio-open failure");
    return 0;
}

#define SDL_OpenAudioDevice probe_audio_open
#define main moonstone_main
#include "../src/moon.c"
#undef main

int main(int argc, char **argv) {
    const char *log = NULL, *record_dir = NULL;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--log")) log = argv[i + 1];
        if (!strcmp(argv[i], "--probe-record-dir")) record_dir = argv[i + 1];
    }
    if (!log) return 2; /* every probe must use an explicit scratch log */
    if (!record_dir) return moonstone_main(argc, argv);
    g_log_path = log;
    g_log = fopen(log, "w");
    if (!g_log) return 2;
    g_sdl_mode = 1;
    snprintf(g_exedir, sizeof(g_exedir), "%s", record_dir);
    SDL_LogSetOutputFunction(sdl_log_output, NULL);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Probe SDL warning routed to file");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 3;
    SDL_Window *win = SDL_CreateWindow("Moonstone recording warning ownership test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 768, SDL_WINDOW_SHOWN);
    if (!win) return 3;
    SDL_SysWMinfo info;
    SDL_zero(info);
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return 3;
    toggle_record(win); /* same path as F12; destination is deliberately absent */
    int rc = g_wav || !IsWindowEnabled(info.info.win.window) ? 2 : 0;
    /* The test runner checks ownership/stacking before dismissing the warning.
     * Then verify the window is usable and a subsequent recording can succeed. */
    if (!rc && !CreateDirectoryA(record_dir, NULL)) rc = 4;
    if (!rc) {
        toggle_record(win);
        if (!g_wav || !strstr(SDL_GetWindowTitle(win), "[REC 1]")) rc = 4;
        if (g_wav) toggle_record(win);
        if (strcmp(SDL_GetWindowTitle(win), g_wintitle)) rc = 4;
    }
    if (!rc) fprintf(g_log, "Probe game window re-enabled; recording retry succeeded\n");
    SDL_DestroyWindow(win);
    SDL_Quit();
    fclose(g_log); g_log = NULL;
    return rc;
}
