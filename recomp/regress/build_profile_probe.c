/* Test-only observer of the SAME emulation code in both build profiles.
 * No recorder, scripting or debug CLI is required in the public engine.
 * This observer is NEVER a distribution executable. */
#define main moonstone_main
#ifndef MOON_PROBE_ENGINE
#define MOON_PROBE_ENGINE "../src/moon.c"
#endif
#include MOON_PROBE_ENGINE
#undef main
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(2); } } while (0)

static uint64_t hash(uint64_t h, const void *p, size_t n) {
    const unsigned char *s=p;
    for (size_t i=0;i<n;i++) { h^=s[i]; h*=1099511628211ull; }
    return h;
}
static void input(const char *mode, int frame) {
    /* The established town-entry/exit/purchase replay, without the dev parser. */
    if (!strcmp(mode,"town")) {
        g_fire=g_fire2=(frame>=10 && frame<16) || (frame>=504 && frame<510);
        g_ji_dn=frame>=450 && frame<494;
    } else if (!strcmp(mode,"armor")) {
        g_ji_up=frame>=10 && frame<36; g_ji_rt=frame>=40 && frame<46;
        g_ji_dn=frame>=800 && frame<844; g_ji_lf=frame>=850 && frame<853;
        g_fire=g_fire2=(frame>=60 && frame<66) || (frame>=860 && frame<866);
        if (frame==700) g_ram[0x2e83b]=0x1e;
    }
    g_mouse_dx=(g_ji_rt-g_ji_lf)*3; g_mouse_dy=(g_ji_dn-g_ji_up)*3;
}
int main(int argc, char **argv) {
    /* data, fixture (or - for boot), frame count, input mode, output prefix, --log PATH */
    if (argc!=8 || strcmp(argv[6],"--log")) return 2;
    g_log_path=argv[7]; g_log=fopen(g_log_path,"w"); CHECK(g_log);
    g_dataset=argv[1]; g_diskdir=argv[1]; g_os=1; g_oslog=1;
    CHECK(ensure_boot_modules(g_dataset,g_diskdir)); detect_lineage(g_dataset);
    char path[1600], error[512]; Module module;
    snprintf(path,sizeof(path),"%s/nb",g_dataset);
    CHECK(!load_hunk(g_ram,RAM_SIZE,path,0x21000,8,&module,error,sizeof(error)));
    setup_amigaos(0x80000); g_blt_busy_scope=1;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_pulse_reset();
    m68k_set_reg(M68K_REG_PC,module.entry); m68k_set_reg(M68K_REG_ISP,0x7f000);
    m68k_set_reg(M68K_REG_USP,0x80000); m68k_set_reg(M68K_REG_SR,0x2000); m68k_set_reg(M68K_REG_SP,0x80000);
    if (strcmp(argv[2],"-")) { g_bootphase=0; CHECK(load_state(argv[2])); }
    g_audio_on=1;
    uint64_t pixels=14695981039346656037ull, pcm=pixels;
    int frames=atoi(argv[3]); CHECK(frames>0);
    for (int fr=0;fr<frames;fr++) {
        g_cur_frame=fr; input(argv[4],fr); autoswap_tick(); run_one_frame(); CHECK(!g_stop);
        capture_frame(); CHECK(g_cap_valid && g_cap_w>0 && g_cap_h>0);
        for (int y=0;y<g_cap_h;y++) pixels=hash(pixels,g_cap+y*FB_W*3,(size_t)g_cap_w*3);
        pcm=hash(pcm,g_frame_buf,sizeof(g_frame_buf));
    }
    snprintf(path,sizeof(path),"%s.sav",argv[5]); CHECK(save_state(path));
    /* A bad load must not partially replace the current machine. */
    uint64_t ram=hash(14695981039346656037ull,g_ram,RAM_SIZE);
    CHECK(!load_state("this-test-fixture-does-not-exist.sav"));
    CHECK(ram==hash(14695981039346656037ull,g_ram,RAM_SIZE));
    snprintf(path,sizeof(path),"%s.txt",argv[5]); FILE *out=fopen(path,"w"); CHECK(out);
    fprintf(out,"frames=%d ram=%016llx pixels=%016llx pcm=%016llx pc=%08x ic=%llu\n",
            frames,(unsigned long long)ram,(unsigned long long)pixels,(unsigned long long)pcm,
            (unsigned)m68k_get_reg(NULL,M68K_REG_PC),(unsigned long long)g_icount);
    CHECK(!fclose(out)); CHECK(!fclose(g_log)); return 0;
}
