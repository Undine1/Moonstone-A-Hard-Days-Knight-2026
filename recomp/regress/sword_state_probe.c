/* Test-only save/load checks against the actual host. Never deploy.
 * A separate scratch --log is mandatory, including for the included main. */
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef NDEBUG
#include <assert.h>

static uint8_t *snapshot(size_t *size) {
    uint32_t regs[SAVE_NREGS];
    for (int i = 0; i < SAVE_NREGS; i++) regs[i] = m68k_get_reg(NULL, SAVE_REGS[i]);
    *size = sv_payload_size(SAVE_VERSION);
    uint8_t *bytes = malloc(*size);
    SvCursor cursor = { bytes, *size, 0, 1 };
    assert(bytes && sv_serialize(&cursor, regs, SAVE_NREGS, SAVE_VERSION));
    return bytes;
}

static void gift_to(unsigned actor) {
    w32(0x2ebd0, actor);
    w32(0x391a8, 0xacfb);
    w32(0x37f00, 0); /* preceding non-sword prize */
    w32(0x1ff000, 0x1ef000);
    w16(0x1ef000, 0x60fe);
    m68k_set_reg(M68K_REG_SR, 0x2700);
    m68k_set_reg(M68K_REG_A7, 0x1ff000);
    m68k_set_reg(M68K_REG_D3, 0);
    m68k_set_reg(M68K_REG_PC, 0x2aa9a);
    m68k_execute(100000);
    assert(m68k_get_reg(NULL, M68K_REG_PC) == 0x1ef000);
}

int main(int argc, char **argv) {
    const char *dir = NULL;
    int n = 1, have_log = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sword-state-dir") && i + 1 < argc) dir = argv[++i];
        else {
            if (!strcmp(argv[i], "--log") && i + 1 < argc) have_log = 1;
            argv[n++] = argv[i];
        }
    }
    if (!have_log || !dir) { fprintf(stderr, "Scratch --log and --sword-state-dir required.\n"); return 2; }
    argv[n] = NULL;
    int rc = moonstone_main(n, argv);
    if (rc) return rc;
    g_log = stdout; /* included main closed its scratch log */
    char path[1024];
    snprintf(path, sizeof(path), "%s/flag-one.sav", dir);
    assert(load_state(path) && g_sword_created == 1);
    gift_to(0x100084);
    assert(g_sword_created == 1 && r8(0x100304) == 1 && r8(0x100334) == 0);
    snprintf(path, sizeof(path), "%s/flag-zero.sav", dir);
    assert(load_state(path) && g_sword_created == 0);
    gift_to(0x100000);
    assert(g_sword_created == 1 && r8(0x100304) == 1);
    puts("PASS: warm loads restore zero/one sword history and the next original gift obeys it");

    snprintf(path, sizeof(path), "%s/roundtrip.sav", dir);
    assert(save_state(path));
    g_sword_created = 0;
    assert(load_state(path) && g_sword_created == 1);
    puts("PASS: actual save/load roundtrip retains campaign history");

    const char *bad[] = { "invalid-flag.sav", "truncated.sav", "trailing.sav", "invalid-streamer.sav", "absent.sav" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        size_t nb, na;
        uint8_t *before = snapshot(&nb);
        uint8_t *stream = g_loadbuf;
        snprintf(path, sizeof(path), "%s/%s", dir, bad[i]);
        assert(!load_state(path));
        uint8_t *after = snapshot(&na);
        assert(nb == na && !memcmp(before, after, nb) && stream == g_loadbuf);
        free(before); free(after);
    }
    puts("PASS: invalid flag, truncation, trailing data, streamer failure and missing save leave all live state unchanged");
    g_log = NULL;
    return 0;
}
