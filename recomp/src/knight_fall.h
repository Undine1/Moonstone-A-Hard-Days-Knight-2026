/* The final fall normally follows an expired kneeling pause. A further hit
 * can select that tail early (notably against axe Troggs), leaving the old
 * pause to rewind the animation back to the kneeling pose after one frame.
 * Expire only that obsolete pause, at the animation boundary. Keep the native
 * finishing hit, fall, sound, blood and life-loss commands. No saved host latch
 * is needed, including when loading a save between selection and animation. */
static void knight_fall_hook(uint32_t pc) {
    if (pc != 0x28a7cu && pc != 0x289e8u) return;
    if (!g_os || (g_lineage != LIN_CRACKED && g_lineage != LIN_RETAIL)) return;
    unsigned retail = g_lineage == LIN_RETAIL;
    uint32_t tail = retail ? 0x31792u : 0x319deu;
    uint32_t display = retail ? 0x2ea68u : 0x2eca0u;
    uint32_t descriptor = m68k_get_reg(NULL, M68K_REG_A1);
    static const uint8_t pose[] = {
        0x08,0x15,0x07,0x01,0xff,0xe4, 0x08,0x16,0x15,0x01,0xff,0xe4,
        0x08,0x17,0x28,0x00,0xff,0xc5, 0xff,0x00,
        0xa4,0x0a,0x08,0x19,0x2a,0x00,0xff,0xcc
    };
    if (pc != (retail ? 0x289e8u : 0x28a7cu) || r32(pc) != 0x2f094aa9u
        || r16(pc + 4u) != 2u
        || descriptor < display || descriptor - display >= 500u
        || (descriptor - display) % 50u || !r8(descriptor) || !r8(descriptor + 1u)
        || r32(descriptor + 2u) != tail
        || r16(tail - 24u) != (retail ? 0xcc00u : 0xd000u)
        || r16(tail - 22u) != 0x8814u
        || memcmp(g_ram + tail - 20u, pose, sizeof(pose)) != 0) return;
    uint32_t actor = r32(descriptor + 0x18u);
    uint32_t work = r32(descriptor + 0x24u);
    if (!actor || (actor & 1u) || actor > RAM_SIZE - 0x84u
        || (r8(actor + 0x4du) != 0xcu && r8(actor + 0x4du) != 0x10u)
        || (int16_t)r16(actor + 0x50u) > 0
        || !work || (work & 1u) || work > RAM_SIZE - 36u
        || r8(work + 1u) != 1u || r8(work) < 1u || r8(work) > 20u
        || r32(work + 2u) != tail - 20u) return;
    /* Exactly the countdown/enable state of an expired native pause. Other
     * workspace fields (repeats, motion, callbacks) and all CPU state survive. */
    w16(work, 0);
}
