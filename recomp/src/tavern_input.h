/* The native tavern samples fire only after its two-frame idle animation
 * (14 PAL frames). Remember a short tap from the existing per-frame cursor
 * reader, then supply it at that SAME native poll. No extra input/RNG calls,
 * animation updates, CPU-cycle shortcuts or changes to the wager machinery.
 * Transient input is discarded on load, focus loss and device/owner changes. */
static struct {
    uint32_t actor, target;
    int previous, owner, device;
} g_tavern_input;
static int g_tavern_focused = 1;

static void tavern_input_reset(void) { memset(&g_tavern_input, 0, sizeof(g_tavern_input)); }

static uint32_t tavern_input_actor(void) {
    if (!g_os || !g_tavern_focused || g_mp_practice
        || (g_lineage != LIN_CRACKED && g_lineage != LIN_RETAIL)) return 0;
    uint32_t cb = mp_addr(0x2b63a, 0x2b512);
    if (r16(cb) != 0x23c8 || r16(cb+6) != 0x4a79
        || r16(cb+0x10) != 0x4eb9 || r32(cb+0x16) != 0x08010004
        || r16(cb+0x4c) != 0x4eb9 || r16(cb+0x7e) != 0x2079) return 0;
    uint32_t state = r32(cb+8), exit = r32(cb+0x6e), holder = r32(cb+0x80);
    if ((state|exit|holder)&1u || state>RAM_SIZE-2 || exit>RAM_SIZE-2 || holder>RAM_SIZE-4
        || r16(state) || r16(exit)) return 0;
    uint32_t actor = r32(holder), roster = mp_addr(0x2e7dc, 0x2e5b4);
    if (actor<roster || actor-roster>=4*0x84u || (actor-roster)%0x84u
        || r32(actor+0x36)>=4) return 0;
    if (g_mp_campaign && (g_mp.phase!=MP_PLAY || g_mp_ui_player<0
        || g_mp_ui_player>=g_mp.players || g_mp.device[g_mp_ui_player]==MP_NONE
        || actor!=roster+0x84u*(unsigned)g_mp_ui_player)) return 0;
    /* These globals also survive leaving the tavern. Require its live native
     * caller, including while an interrupt/cursor callback is on the stack. */
    return mp_guest_call_active(mp_addr(0x2b318, 0x2b1f0)) ? actor : 0;
}

static uint32_t tavern_input_target(void) {
    uint32_t cb = mp_addr(0x2b63a, 0x2b512);
    uint32_t xy = r32(cb+0x42), hit = r32(cb+0x4e);
    if ((xy|hit)&1u || xy>RAM_SIZE-4 || hit>RAM_SIZE-0x16 || r16(hit+0x10)!=0x2079) return 0;
    uint32_t list = r32(hit+0x12);
    if ((list&1u) || list>RAM_SIZE-4) return 0;
    uint32_t table = r32(list);
    if ((table&1u) || table>RAM_SIZE-6*24u) return 0;
    unsigned x=r16(xy), y=r16(xy+2);
    for (unsigned i=0; i<6; i++) {
        uint32_t h=table+24*i;
        unsigned left=r16(h+12), top=r16(h+14), width=r16(h+4), height=r16(h+6);
        /* Only the original five stakes and Exit, with their original geometry.
         * A moved pointer cancels the queued tap rather than buying another bet. */
        static const unsigned tops[]={38,66,94,121,147,181};
        if (left!=268 || top!=tops[i] || width!=(i<5?43u:45u) || height!=(i<5?24u:16u)
            || r32(h+16)!=(i<5?1u:2u) || (i<5 && r16(h+20)!=i+1)) return 0;
        if (x>=left && x<=left+width && y>=top && y<=top+height) return h;
    }
    return 0;
}

static int tavern_input_sync(unsigned fire) {
    uint32_t actor=tavern_input_actor();
    int owner=g_mp_campaign?g_mp_ui_player:-1;
    int device=g_mp_campaign && owner>=0 && owner<MP_MAX_PLAYERS?g_mp.device[owner]:g_primary_pad;
    if (!actor) { tavern_input_reset(); g_tavern_input.previous=!!fire; return 0; }
    if (g_tavern_input.actor && (g_tavern_input.actor!=actor
        || g_tavern_input.owner!=owner || g_tavern_input.device!=device)) {
        tavern_input_reset(); g_tavern_input.previous=!!fire;
    }
    g_tavern_input.actor=actor; g_tavern_input.owner=owner; g_tavern_input.device=device;
    return 1;
}

static void tavern_input_hook(uint32_t pc) {
    if (pc!=0x2d6c8 && pc!=0x2d49a && pc!=0x2b650 && pc!=0x2b528
        && pc!=0x2b318 && pc!=0x2b1f0 && pc!=0x2b574 && pc!=0x2b44c
        && pc!=0x2b5f4 && pc!=0x2b4cc && pc!=0x2b6d0 && pc!=0x2b5a8) return;
    if (!g_os || (g_lineage!=LIN_CRACKED && g_lineage!=LIN_RETAIL)) return;
    uint32_t sample=mp_addr(0x2d6c8,0x2d49a), poll=mp_addr(0x2b650,0x2b528);
    if (pc==sample) {
        if (r16(pc)!=(g_lineage==LIN_RETAIL?0x7000:0x303c)) return;
        unsigned fire=m68k_get_reg(NULL,M68K_REG_D1)&16u;
        if (!tavern_input_sync(fire)) return;
        if (fire && !g_tavern_input.previous && !g_tavern_input.target)
            g_tavern_input.target=tavern_input_target();
        g_tavern_input.previous=!!fire;
    } else if (pc==poll) {
        unsigned input=m68k_get_reg(NULL,M68K_REG_D1);
        if (!tavern_input_sync(input&16u)) return;
        if (!(input&16u) && g_tavern_input.target
            && g_tavern_input.target==tavern_input_target())
            m68k_set_reg(M68K_REG_D1,input|16u);
        /* Held input keeps its original behavior; either path consumes the tap.
         * Only D1's fire bit changes. Native BTST/branches/transaction do the rest. */
        g_tavern_input.target=0;
    } else if (pc==mp_addr(0x2b318,0x2b1f0) || pc==mp_addr(0x2b574,0x2b44c)
            || pc==mp_addr(0x2b5f4,0x2b4cc) || pc==mp_addr(0x2b6d0,0x2b5a8)) {
        tavern_input_reset();
        g_tavern_input.previous=!!g_fire2; /* Do not queue the opening/acknowledgement press. */
    }
}
