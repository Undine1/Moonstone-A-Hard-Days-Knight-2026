/* A spear Trogg's paired execution replaces its animation and removes the
 * knight. The original owner lookup can select a discarded record left by an
 * earlier wave, so the execution never runs and the fight never ends. Select
 * the unique live record in THIS call only; other animation lookups retain
 * their native behavior. No damage, timing or death-script changes. */
static uint32_t spear_death_live_display(uint32_t actor, uint32_t table) {
    if (!actor || (actor & 1u) || actor > RAM_SIZE - 0x84u
        || !r32(actor) || r8(actor + 0x4du) != 0x20u
        || (int16_t)r16(actor + 0x50u) <= 0) return 0;
    uint32_t found = 0;
    for (unsigned i = 0; i < 10u; i++) {
        uint32_t d = table + i * 50u;
        if (r8(d) && r32(d + 0x18u) == actor) {
            if (found) return 0; /* ambiguous/corrupt state: do not guess */
            found = d;
        }
    }
    return found;
}

static void spear_death_hook(uint32_t pc) {
    if (pc != 0x28846u && pc != 0x287d8u && pc != 0x288c8u && pc != 0x2885au) return;
    if (!g_os || (g_lineage != LIN_CRACKED && g_lineage != LIN_RETAIL)) return;
    unsigned retail = g_lineage == LIN_RETAIL;
    uint32_t table = retail ? 0x2ea68u : 0x2eca0u;
    uint32_t lookup = retail ? 0x287d8u : 0x28846u;
    uint32_t dispatch = retail ? 0x2885au : 0x288c8u;
    uint32_t kill = retail ? 0x32940u : 0x32b90u;
    uint32_t caller = retail ? 0x266e2u : 0x265c4u;
    uint32_t updater = retail ? 0x2873au : 0x2877cu;
    if (r32(lookup) != 0xb0ae0018u || r16(lookup + 4u) != 0x6710u
        || r16(dispatch) != 0x4df9u || r32(dispatch + 2u) != table
        || r16(caller) != 0x4eb9u || r32(caller + 2u) != updater
        || r16(caller - 6u) != 0x207cu || r32(caller - 4u) != kill
        || r16(kill) != 0xb000u || r32(kill + 2u) != 0x213f4u) return;

    if (pc == lookup) {
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
        uint32_t d = m68k_get_reg(NULL, M68K_REG_A6);
        uint32_t actor = m68k_get_reg(NULL, M68K_REG_D0);
        if ((sp & 1u) || sp > RAM_SIZE - 12u
            || r32(sp + 4u) != updater + 6u || r32(sp + 8u) != caller + 6u
            || m68k_get_reg(NULL, M68K_REG_A0) != kill
            || d < table || d - table >= 500u || (d - table) % 50u
            || r8(d) || r32(d + 0x18u) != actor
            || !spear_death_live_display(actor, table)) return;
        m68k_set_reg(M68K_REG_PC, lookup + 6u); /* original next-record path */
        return;
    }
    if (pc != dispatch || g_mp_practice) return;

    /* Old saves can be AFTER the failed lookup. An inactive record with the
     * untouched execution entry, newly-enabled animation byte and completely
     * cleared workspace is the original updater's unconsumed result. Move that
     * pending assignment to its unique live owner, using the updater's exact
     * workspace/pointer writes. Run the authored execution normally; do not
     * synthesize HP/life loss, end a fight early, or revive the dead knight. */
    if (r16(0x21380u) != 0x45f9u) return;
    uint32_t fight = r32(0x21382u);
    if (!fight || (fight & 1u) || fight > RAM_SIZE - 0x20u
        || r8(fight + 8u) != 1u || r8(fight + 0x10u)) return;
    uint32_t victim = r32(fight);
    if (!victim || (victim & 1u) || victim > RAM_SIZE - 0x84u
        || r32(fight + 4u) != victim || r8(victim + 0x4du) != 0xcu
        || r32(victim) || (int16_t)r16(victim + 0x50u) > 0) return;
    uint32_t pending = 0, live = 0;
    for (unsigned i = 0; i < 10u; i++) {
        uint32_t d = table + i * 50u;
        if (r8(d) && r32(d + 0x18u) == victim) return;
        if (r8(d) || r8(d + 1u) != 1u || r32(d + 2u) != kill || r16(d + 0x30u)) continue;
        uint32_t w = r32(d + 0x24u);
        if (!w || (w & 1u) || w > RAM_SIZE - 36u) continue;
        static const uint8_t empty[36] = {0};
        if (memcmp(g_ram + w, empty, sizeof(empty))) continue;
        uint32_t target = spear_death_live_display(r32(d + 0x18u), table);
        if (!target) continue;
        if (pending) return;
        pending = d; live = target;
    }
    if (!pending) return;
    uint32_t work = r32(live + 0x24u);
    if (!work || (work & 1u) || work > RAM_SIZE - 36u || r16(live + 0x30u)) return;
    memset(g_ram + work, 0, 36);
    w32(live + 2u, kill);
    w8(live + 1u, 1);
    w8(pending + 1u, 0); /* consume the abandoned assignment exactly once */
    if (g_log) {
        static unsigned reports;
        if (reports++ < 4u) fprintf(g_log, "SPEAR-DEATH resumed pending execution actor=%06x victim=%06x fr=%d\n",
                                   r32(live + 0x18u), victim, g_cur_frame);
    }
}
