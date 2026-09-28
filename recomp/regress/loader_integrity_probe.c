/* Test-only data-loading checks. Never deploy. Every run requires --log. */
#define main moonstone_main
#define moon_instr_hook production_hook
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int inject_short_read;
void moon_instr_hook(unsigned pc) {
    if (inject_short_read && (pc == NB_STREAM_READ || pc == PR_STREAM_READ) && r16(pc) == 0x4279u) {
        inject_short_read = 0;
        g_loadsize = g_loadpos; /* simulate a stream ending early, without altering any file */
    }
    production_hook(pc);
}

static void reset_stream(long pos) {
    free(g_loadbuf); g_loadbuf = malloc(8); assert(g_loadbuf);
    memcpy(g_loadbuf, "testdata", 8); g_loadsize = 8; g_loadpos = pos;
    snprintf(g_loadname, sizeof(g_loadname), "program");
    g_stop = g_data_read_failed = 0; g_stop_reason = "?";
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_reg(M68K_REG_SR, 0x2700); m68k_set_reg(M68K_REG_PC, 0x21234);
    m68k_set_reg(M68K_REG_SP, 0x1ff000); w32(0x1ff000, 0x21300);
    m68k_set_reg(M68K_REG_A0, 0x140000); memset(g_ram + 0x140000, 0xa5, 32);
}

static void write_fixture(const char *path, const char *bytes, size_t count) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(bytes, 1, count, f) == count); assert(fclose(f) == 0);
}

static void stream_tests(const char *dir) {
    unsigned cases = 0;
    g_dataset = dir;
    for (unsigned skip = 0; skip < 2; skip++) {
        reset_stream(0); m68k_set_reg(M68K_REG_D0, 8);
        if (skip) hle_stream_skip(); else hle_stream_read();
        assert(!g_stop && g_loadpos == 8 && m68k_get_reg(NULL, M68K_REG_PC) == 0x21300);
        if (!skip) assert(!memcmp(g_ram + 0x140000, "testdata", 8));
        cases++;
        reset_stream(8); m68k_set_reg(M68K_REG_D0, 0);
        if (skip) hle_stream_skip(); else hle_stream_read();
        assert(!g_stop && g_loadpos == 8); cases++;
        const long positions[] = {0, 8, -1, 9, 0};
        const uint32_t amounts[] = {9, 1, 1, 1, 0xffffffffu};
        for (unsigned i = 0; i < 5; i++) {
            reset_stream(positions[i]); m68k_set_reg(M68K_REG_D0, amounts[i]);
            if (skip) hle_stream_skip(); else hle_stream_read();
            assert(g_stop && g_data_read_failed && g_loadpos == positions[i]);
            assert(strstr(g_data_read_path, "program") && strstr(g_data_read_reason, "Incomplete game data"));
            for (unsigned b = 0; b < 32; b++) assert(g_ram[0x140000 + b] == 0xa5);
            assert(r16(0x2ca22) == 0xffff && r16(0x2ad4a) == 0xffff);
            assert(m68k_get_reg(NULL, M68K_REG_PC) == 0x21234); cases++;
        }
    }
    reset_stream(3);
    g_boot_data[1] = malloc(8); assert(g_boot_data[1]);
    memcpy(g_boot_data[1], "testdata", 8); g_boot_size[1] = 8;
    assert(boot_data_matches("DF0:PROGRAM", g_boot_data[1], 8));
    char path[1300]; snprintf(path, sizeof(path), "%s/program", dir);
    size_t len = sv_payload_size(SAVE_VERSION);
    uint8_t *payload = malloc(len), *staged = NULL; assert(payload);
    uint32_t regs[SAVE_NREGS] = {0}; SvCursor save = {payload, len, 0, 1};
    assert(sv_serialize(&save, regs, SAVE_NREGS, SAVE_VERSION));
    write_fixture(path, "testdata", 8);
    assert(sv_prepare_streamer(payload, len, SAVE_VERSION, &staged));
    assert(!memcmp(staged, "testdata", 8)); free(staged); cases++;
    write_fixture(path, "testdatX", 8);
    assert(!sv_prepare_streamer(payload, len, SAVE_VERSION, &staged) && !staged); cases++;
    write_fixture(path, "test", 4);
    assert(!sv_prepare_streamer(payload, len, SAVE_VERSION, &staged) && !staged); cases++;
    for (unsigned fault = 0; fault < 4; fault++) {
        reset_stream(0);
        if (fault == 3) assert(remove(path) == 0);
        else write_fixture(path, fault == 1 ? "testdatX" : "testdata", fault == 2 ? 4 : 8);
        memcpy(g_ram + 0x180000, "program", 8);
        m68k_set_reg(M68K_REG_A0, 0x180000);
        hle_load_by_name();
        assert(g_stop == (fault != 0) && g_data_read_failed == (fault != 0));
        if (!fault) assert(g_loadsize == 8 && !memcmp(g_loadbuf, "testdata", 8));
        else assert(!g_loadbuf && !g_loadsize);
        cases++;
    }
    free(payload);
    printf("PASS %u stream bounds and saved-stream validation cases\n", cases);
}

int main(int argc, char **argv) {
    const char *unit = NULL, *log = NULL; int n = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--probe-loader-short")) inject_short_read = 1;
        else if (!strcmp(argv[i], "--probe-stream-tests") && i + 1 < argc) unit = argv[++i];
        else { if (!strcmp(argv[i], "--log") && i + 1 < argc) log = argv[i + 1]; argv[n++] = argv[i]; }
    }
    if (!log) return 2;
    argv[n] = NULL;
    if (!unit) return moonstone_main(n, argv);
    g_log_path = log; g_log = fopen(log, "w"); if (!g_log) return 2;
    stream_tests(unit);
    fclose(g_log); g_log = NULL; return 0;
}
