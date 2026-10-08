/* Retail keeps Acquisition/Wyrm in the existing inventory loop. The earlier
 * module tail-calls another inventory instead. Port the connected mode and
 * re-entry transitions; leave transactions, drawing and final cleanup native.
 *
 * Retail's re-entry word has no corresponding storage in the earlier module.
 * A tagged local word on the guest stack carries that request through the
 * native wait/tick, including save/load. It is consumed before rebuilding the
 * panel. No unsaved host phase, new save field or nested inventory call.
 * --noinventoryflow is the diagnostic A/B switch. */
static int g_inventory_flow = 1;
#define INVENTORY_REENTER_TAG 0x494e5631u /* "INV1", saved with the guest stack */

static int inventory_flow_layout(void) {
    return r32(0x2bbecu) == 0x33fc0001u
        && r32(0x2bc02u) == 0x33fc0000u && r32(0x2bc06u) == 0x0002fb02u
        && r32(0x2bc10u) == 0x33fc0000u && r32(0x2bc14u) == 0x000392e6u
        && r16(0x2bc18u) == 0x4eb9u && r32(0x2bc1au) == 0x00029b26u
        && r16(0x2bc52u) == 0x6100u && r16(0x2bc54u) == 0x112au
        && r16(0x2bc56u) == 0x4a79u && r32(0x2bc58u) == 0x000392e6u
        && r16(0x2bc64u) == 0x4eb9u && r32(0x2bc66u) == 0x000293c4u
        && r16(0x2bc6au) == 0x60c4u && r16(0x2bca2u) == 0x4e75u;
}

/* A save made by the old engine can contain one or more scroll tail-call
 * returns. At an inventory boundary these contain only 2bc56; the inventory
 * routine pushes no local registers. Remove exactly those frames, and only
 * when they end at a verified JSR to the inventory entry. This converts the
 * old call structure to the same single loop used by new casts, without
 * modifying inventory contents, replaying transactions or forcing an exit. */
static void inventory_flow_legacy_stack(void) {
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7), end = sp;
    if ((sp & 1u) || sp > RAM_SIZE - 4u) return;
    while (end <= RAM_SIZE - 4u && r32(end) == 0x2bc56u) end += 4u;
    if (end == sp || end > RAM_SIZE - 4u) return;
    uint32_t caller = r32(end);
    if ((caller & 1u) || caller < 6u || caller >= RAM_SIZE
        || r16(caller - 6u) != 0x4eb9u || r32(caller - 4u) != 0x2bbecu) return;
    m68k_set_reg(M68K_REG_A7, end);
}

static void inventory_flow_hook(unsigned pc) {
    if (!g_os || g_lineage != LIN_CRACKED) return;
    if (pc != 0x2cf18u && pc != 0x2cf1eu && pc != 0x2cf96u && pc != 0x2cf9cu
        && pc != 0x2bc6au && pc != 0x2bbecu && pc != 0x2bc30u
        && pc != 0x2bc6cu && pc != 0x2bca2u && pc != 0x2bd7eu) return;
    if (!inventory_flow_layout()) return;

    uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
    if (pc == 0x2bc6au && !(sp & 1u) && sp <= RAM_SIZE - 4u
        && r32(sp) == INVENTORY_REENTER_TAG) {
        /* Finish an already-started transition even if an A/B option changed
         * across load. Retail tests its request after the same wait/tick. */
        uint32_t active = r32(0x2bbf0u);
        if ((active & 1u) || active > RAM_SIZE - 2u) return;
        m68k_set_reg(M68K_REG_A7, sp + 4u);
        w16(active, 1);
        w16(0x2fb02u, 0);
        w16(0x392e6u, 0);
        m68k_set_reg(M68K_REG_SR, (m68k_get_reg(NULL, M68K_REG_SR) & ~15u) | 4u);
        g_inventory_menu_active = g_inventory_close_request = 0;
        m68k_set_reg(M68K_REG_PC, 0x2bc18u);
        return;
    }
    if (!g_retail_parity || !g_inventory_flow) return;
    if (pc == 0x2bbecu || pc == 0x2bc30u || pc == 0x2bc6cu || pc == 0x2bca2u) {
        inventory_flow_legacy_stack();
    } else if (pc == 0x2bd7eu) {
        /* Retail updates both modes when completed knight loot becomes the
         * winner's own inventory (2bc60..2bc74 in the retail module). */
        if (r32(0x2bd74u) == 0x23fc0000u && r16(0x2bd78u) == 9u
            && r32(0x2bd7au) == 0x0002fb1cu && r16(pc) == 0x6000u)
            w32(0x2fb04u, 9);
    } else if (pc == 0x2cf18u || pc == 0x2cf1eu || pc == 0x2cf96u || pc == 0x2cf9cu) {
        unsigned start = pc < 0x2cf90u ? 0x2cf18u : 0x2cf96u;
        unsigned mode = start == 0x2cf18u ? 8u : 11u;
        if (r32(start) != 0x203c0000u || r16(start + 4u) != mode
            || r16(start + 6u) != 0x4ef9u || r32(start + 8u) != 0x2bbecu
            || (sp & 1u) || sp > RAM_SIZE - 4u || r32(sp) != 0x2bc56u) return;
        if (mode == 11u) w32(0x2fb04u, r32(0x2fb1cu));
        w32(0x2fb1cu, mode);
        /* Replace the dispatched action's return with the loop's local
         * request, then return to the ordinary exit-test/wait/tick path.
         * Retail's final MOVE.W #1 preserves X and clears N/Z/V/C. */
        w32(sp, INVENTORY_REENTER_TAG);
        m68k_set_reg(M68K_REG_SR, m68k_get_reg(NULL, M68K_REG_SR) & ~15u);
        g_inventory_menu_active = g_inventory_close_request = 0;
        m68k_set_reg(M68K_REG_PC, 0x2bc56u);
    }
}
