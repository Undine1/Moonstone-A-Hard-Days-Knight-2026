/* Stable standing-body obstruction during living axe-Trogg recoil.
 * This deliberately corrects behavior shared by both original editions.
 * Never change the animation-owned actor bounds: scenery and attack/render
 * paths must retain their original inputs. Limit only excessive horizontal
 * reach in human movement checks. Intersect with the current bounds, so this
 * cannot introduce a new obstruction on the far side. No saved host state. */
static int trogg_recoil_bounds(uint32_t descriptor, uint32_t actor, int box[2]) {
    static const uint8_t standing[] = {
        0x00,0x00,0x04,0x01,0xff,0xe6, 0x00,0x01,0x1e,0x01,0x00,0x0b,
        0x04,0x45,0xf4,0x00,0xff,0xef, 0xff,0xff
    };
    unsigned retail = g_lineage == LIN_RETAIL;
    uint32_t idle = retail ? 0x32c9eu : 0x32eeeu;
    uint32_t script = r32(descriptor + 2u);
    /* Three contiguous nonlethal hit-reaction scripts. Include the terminating
     * pose when active=0: its bounds persist until the next animation update.
     * Death branches are outside this range and HP must still be positive. */
    if (!actor || (actor & 1u) || actor > RAM_SIZE - 0x84u
        || r8(actor + 0x4du) != 0x1cu || (int16_t)r16(actor + 0x50u) <= 0
        || script < idle + 0x1f4u || script >= idle + 0x372u
        || (script & 1u) || r32(actor + 0x16u) != idle
        || memcmp(g_ram + idle, standing, sizeof(standing)) != 0) return 0;
    uint32_t reactions = r32(actor + 0x1eu);
    if (!reactions || (reactions & 1u) || reactions > RAM_SIZE - 0x24u
        || r32(reactions + 4u) != idle + 0x2fcu
        || r32(reactions + 8u) != idle + 0x286u
        || r32(reactions + 12u) != idle + 0x1f4u) return 0;
    uint32_t bank = r32(actor + 0x26u);
    unsigned facing = r8(actor + 0xau);
    if (!bank || (bank & 1u) || bank > RAM_SIZE - 8u
        || r32(descriptor + 0x1cu) != bank || (facing != 1u && facing != 3u)) return 0;
    int x = (int16_t)r16(actor + 4u);
    for (unsigned part = 0; part < 3u; part++) {
        const uint8_t *p = standing + 6u * part;
        uint32_t image = r32(bank + p[0]);
        uint32_t frame = image + 10u * p[1] + 14u;
        if (!image || (image & 1u) || image > RAM_SIZE - 10u * p[1] - 18u) return 0;
        int width = r16(frame);
        if (width < 1 || width > 320) return 0;
        int dx = (int16_t)((unsigned)p[4] * 256u + p[5]);
        int left = facing == 1u ? x + dx : x - dx - width;
        if (left < -32768 || left + width > 32767) return 0;
        if (!part) { box[0] = left; box[1] = left + width; }
        else {
            if (left < box[0]) box[0] = left;
            if (left + width > box[1]) box[1] = left + width;
        }
    }
    int current_left = (int16_t)r16(actor + 0x3au);
    int current_right = (int16_t)r16(actor + 0x3cu);
    /* The original overlap helper zero-extends these words before comparing.
     * A pose crossing x=0 therefore has wrapped bounds, not an ordered range.
     * Preserve that native edge behavior: clipping it could ADD obstruction. */
    if (current_left < 0 || current_right <= current_left) return 0;
    if (box[0] < current_left) box[0] = current_left;
    if (box[1] > current_right) box[1] = current_right;
    /* Keep the native behavior for an empty/malformed intersection, rather
     * than allowing movement through a body with no usable standing overlap. */
    return box[0] < box[1];
}

static void trogg_movement_hook(uint32_t pc) {
    switch (pc) {
        case 0x295ccu: case 0x29610u: case 0x29480u: case 0x294c4u: break;
        default: return;
    }
    if (!g_os || (g_lineage != LIN_CRACKED && g_lineage != LIN_RETAIL)) return;
    unsigned retail = g_lineage == LIN_RETAIL;
    uint32_t normalized = pc + (retail ? 0x14cu : 0u);
    if (normalized != 0x295ccu && normalized != 0x29610u) return;
    if (r32(pc - 8u) != 0x3429003au || r32(pc - 4u) != 0x3629003cu
        || r16(pc) != 0x4eb9u || r32(pc + 2u) != (retail ? 0x29696u : 0x297e2u)) return;
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    /* Reject AI/other callers, including use of the same overlap helper for
     * attacks. The two native stack returns identify human movement only. */
    if ((sp & 1u) || sp > RAM_SIZE - 8u
        || r32(sp) != (retail ? 0x29414u : 0x29560u)
        || r32(sp + 4u) != (retail ? 0x26324u : 0x26204u)) return;
    uint32_t human = m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t actor = m68k_get_reg(NULL, M68K_REG_A1);
    uint32_t descriptor = m68k_get_reg(NULL, M68K_REG_A6);
    uint32_t display = retail ? 0x2ea68u : 0x2eca0u;
    if (!human || (human & 1u) || human > RAM_SIZE - 0x84u || human == actor
        || r8(human + 0x4du) != 0xcu || (int16_t)r16(human + 0x50u) <= 0
        || r32(retail ? 0x2e9acu : 0x2ebd0u) != human
        || descriptor < display || descriptor - display >= 500u
        || (descriptor - display) % 50u || !r8(descriptor)
        || r32(descriptor + 0x18u) != actor) return;
    int box[2];
    if (!trogg_recoil_bounds(descriptor, actor, box)) return;
    m68k_set_reg(M68K_REG_D2, (m68k_get_reg(NULL, M68K_REG_D2) & 0xffff0000u) | (uint16_t)box[0]);
    m68k_set_reg(M68K_REG_D3, (m68k_get_reg(NULL, M68K_REG_D3) & 0xffff0000u) | (uint16_t)box[1]);
    /* JSR leaves CCR alone; the first native CMP overwrites flags before use. */
}
