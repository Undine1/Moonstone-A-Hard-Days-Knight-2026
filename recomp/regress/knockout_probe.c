/* Test-only instruction/frame observations and actual save/load checks.
 * Never deploy this executable. Every invocation requires a scratch --log. */
#define moon_instr_hook knockout_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int ko_trace, ko_last_frame = -1;
static uint32_t ko_snapshot_pc;
static const char *ko_snapshot_path;

void moon_instr_hook(unsigned pc) {
    if (ko_trace && g_cur_frame != ko_last_frame) {
        ko_last_frame = g_cur_frame;
        fprintf(g_log, "KO-FRAME fr=%d latch=%u active=%u countdown=%u",
                g_cur_frame, g_ko_latch, r8(0x2e0c4), r8(0x2e0cc));
        for (unsigned i = 0; i < 10; i++) {
            unsigned d = 0x2eca0 + i * 0x32, actor = r32(d + 0x18);
            if (r8(d) && actor && actor < RAM_SIZE - 0x84)
                fprintf(g_log, " d%u=%06x/%06x/%u/%u/%d/%02x", i, actor,
                        r32(d + 2), r16(d + 0x30), r8(actor + 0x49),
                        (int16_t)r16(actor + 0x50), r8(actor + 0x4d));
        }
        fputc('\n', g_log);
    }
    if (ko_snapshot_path && pc == ko_snapshot_pc) {
        assert(save_state(ko_snapshot_path));
        fprintf(g_log, "KO-SNAPSHOT pc=%06x latch=%u\n", pc, g_ko_latch);
        ko_snapshot_path = NULL;
    }
    if (ko_trace && (pc == 0x2670e || pc == 0x26734 || pc == 0x26b8c
        || pc == 0x26bb8 || pc == 0x21380 || pc == 0x213f4
        || pc == 0x2877c || pc == 0x28874 || pc == 0x26794
        || (pc == 0x27ec8 && (r32(0x2eaf8) == 0x319de || r32(0x2eaf8) == 0x31a3c)))) {
        fprintf(g_log, "KO-STEP pc=%06x fr=%d latch=%u a0=%06x a1=%06x anim=%08x d0=%08x a6=%06x sr=%04x\n",
                pc, g_cur_frame, g_ko_latch, m68k_get_reg(NULL, M68K_REG_A0),
                m68k_get_reg(NULL, M68K_REG_A1), r32(0x2eaf8),
                m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_A6),
                m68k_get_reg(NULL, M68K_REG_SR));
    }
    knockout_original_hook(pc);
}

static uint8_t *ko_machine_snapshot(size_t *size) {
    uint32_t regs[SAVE_NREGS];
    for (int i = 0; i < SAVE_NREGS; i++) regs[i] = m68k_get_reg(NULL, SAVE_REGS[i]);
    *size = sv_payload_size(SAVE_VERSION);
    uint8_t *bytes = malloc(*size);
    SvCursor cursor = { bytes, *size, 0, 1 };
    assert(bytes && sv_serialize(&cursor, regs, SAVE_NREGS, SAVE_VERSION));
    return bytes;
}

static void ko_load_tests(const char *dir) {
    char path[1024];
    const char *valid[] = { "latch-one.sav", "latch-zero.sav", "latch-one.sav",
                           "legacy-v3.sav", "latch-one.sav", "legacy-v2.sav" };
    const int expected[] = { 1, 0, 1, 0, 1, 0 };
    for (unsigned i = 0; i < sizeof(expected)/sizeof(expected[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, valid[i]);
        assert(load_state(path) && g_ko_latch == expected[i]);
    }
    snprintf(path, sizeof(path), "%s/latch-one.sav", dir);
    assert(load_state(path) && g_ko_latch == 1);
    snprintf(path, sizeof(path), "%s/roundtrip.sav", dir);
    assert(save_state(path));
    g_ko_latch = 0;
    assert(load_state(path) && g_ko_latch == 1);
    puts("PASS: warm zero/one restores, legacy neutral state and actual save roundtrip");

    const char *bad[] = { "invalid-ko.sav", "invalid-sword.sav", "truncated.sav",
                         "trailing.sav", "invalid-streamer.sav", "absent.sav" };
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        size_t nb, na;
        uint8_t *before = ko_machine_snapshot(&nb), *stream = g_loadbuf;
        snprintf(path, sizeof(path), "%s/%s", dir, bad[i]);
        assert(!load_state(path));
        uint8_t *after = ko_machine_snapshot(&na);
        assert(nb == na && !memcmp(before, after, nb) && stream == g_loadbuf);
        free(before); free(after);
    }
    puts("PASS: invalid sidecars/files leave the entire running machine unchanged");

    /* Resume the actual second rat-reaction routine after a warm load with
     * each saved value. The fixture's guest return address is a terminal loop. */
    for (int flag = 0; flag <= 1; flag++) {
        snprintf(path, sizeof(path), "%s/latch-%s.sav", dir, flag ? "one" : "zero");
        assert(load_state(path));
        m68k_execute(2000);
        assert(m68k_get_reg(NULL, M68K_REG_PC) == 0x1ef000);
        assert(r32(0x2eaf8) == (flag ? 0xffffffffu : 0x123456u));
    }
    puts("PASS: warm loads drive the next original rat reaction correctly");
}

int main(int argc, char **argv) {
    int n = 1, have_log = 0;
    const char *state_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ko-trace")) ko_trace = 1;
        else if (!strcmp(argv[i], "--ko-state-dir") && i+1 < argc) state_dir = argv[++i];
        else if (!strcmp(argv[i], "--ko-snapshot") && i+2 < argc) {
            ko_snapshot_pc = (unsigned)strtoul(argv[++i], NULL, 16);
            ko_snapshot_path = argv[++i];
        } else {
            if (!strcmp(argv[i], "--log") && i+1 < argc) have_log = 1;
            argv[n++] = argv[i];
        }
    }
    if (!have_log) { fprintf(stderr, "An explicit scratch --log is required.\n"); return 2; }
    argv[n] = NULL;
    int rc = moonstone_main(n, argv);
    if (rc) return rc;
    if (state_dir) {
        ko_trace = 0;
        g_log = stdout; /* included main closed its scratch log */
        ko_load_tests(state_dir);
        g_log = NULL;
    }
    return 0;
}
