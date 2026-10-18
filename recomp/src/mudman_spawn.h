/* Both disk editions draw a Mudman's initial standing pose at X=-50/340
 * before its first AI update relocates it for the underground emergence.
 * The right-hand pose extends onto the screen for a few frames. Suppress
 * only its final pixel writes: animation, sound, collision bounds, CPU cycles,
 * dirty rectangles and blitter bookkeeping still execute normally.
 * Derive everything from the current native call/descriptor; no visibility
 * flag or host latch can leak into its emergence, grabs, deaths or saves. */
static int mudman_spawn_blit_hidden(void) {
    if (!g_os || (g_lineage != LIN_CRACKED && g_lineage != LIN_RETAIL)) return 0;
    unsigned retail = g_lineage == LIN_RETAIL;
    uint32_t ret = retail ? 0x28bf0u : 0x28c9cu;
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
    if ((sp & 1u) || sp > RAM_SIZE - 8u || r32(sp) != ret
        || r16(ret - 6u) != 0x4eb9u || r32(ret) != 0x225f06a9u) return 0;
    uint32_t draw = r32(ret - 4u);
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PPC);
    /* This is the bitmap routine's final BLTSIZE write, after decoding and
     * mask construction. Never suppress scratch-mask or background blits.
     * Read its relocated entry from the caller (flat and live hunks differ). */
    if (draw > RAM_SIZE - 0x3c4u || pc != draw + 0x3beu
        || r32(pc) != 0x33c100dfu || r16(pc + 4u) != 0xf058u) return 0;
    uint32_t d = r32(sp + 4u), table = retail ? 0x2ea68u : 0x2eca0u;
    uint32_t pose = retail ? 0x35acau : 0x35d1au;
    if (d < table || d - table >= 500u || (d - table) % 50u
        || !r8(d) || !r8(d + 1u) || r16(d + 0x30u)) return 0;
    uint32_t piece = r32(d + 2u);
    if (piece < pose || piece - pose >= 30u || (piece - pose) % 6u
        || (r16(d + 6u) != 340u && (int16_t)r16(d + 6u) != -50)) return 0;
    uint32_t actor = r32(d + 0x18u);
    if (!actor || (actor & 1u) || actor > RAM_SIZE - 0x84u
        || !r32(actor) || r8(actor + 0x4du) != 4u
        || (int16_t)r16(actor + 0x50u) <= 0 || r16(actor + 0x68u)) return 0;
    static const uint8_t initial_pose[] = {
        0x00,0x1e,0xf6,0x01,0xff,0xe4, 0x00,0x1d,0x33,0x40,0x00,0x24,
        0x00,0x1c,0x32,0x40,0xff,0xd5, 0x00,0x21,0x2d,0x41,0xff,0xe1,
        0x00,0x20,0x37,0x40,0xff,0xdf, 0xff,0xff
    };
    return memcmp(g_ram + pose, initial_pose, sizeof(initial_pose)) == 0;
}
