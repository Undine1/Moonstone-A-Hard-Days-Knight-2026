/* Original campaign boundaries. Logical players are roster slots (selection
 * order), not knight colours, current SDL indices or the two combat ports.
 * No game rules, roster records, turn counters or save bytes are changed here. */
typedef enum { MP_CAM_NONE, MP_CAM_SELECT, MP_CAM_MAP, MP_CAM_UI, MP_CAM_COMBAT } MpCampaignContext;
static MpCampaignContext g_mp_context;
static int g_mp_campaign, g_mp_ui_player = -1, g_mp_restore_pending;
static unsigned g_mp_combatants;

static int mp_active(void) { return g_mp_practice || g_mp_campaign; }
static uint32_t mp_addr(uint32_t cracked, uint32_t retail) {
    return g_lineage == LIN_RETAIL ? retail : cracked;
}
static int mp_campaign_players(void) {
    uint32_t selector = mp_addr(0x22fc4,0x22f70);
    if (!g_os || r16(selector) != 0x0c28 || r32(selector+2) != 0x0001000b
        || r16(mp_addr(0x3051e,0x302d2)) != 3) return 0;
    int n = r16(mp_addr(0x2e024,0x2ddfc));
    return n >= 2 && n <= MP_MAX_PLAYERS ? n : 0;
}
static int mp_roster_player(uint32_t actor, int human) {
    uint32_t base = mp_addr(0x2e7dc,0x2e5b4);
    if (actor < base || actor >= base + 4*0x84 || (actor-base)%0x84) return -1;
    int p = (int)((actor-base)/0x84);
    if (p >= mp_campaign_players() || (human && r32(actor+0x36) >= 4)) return -1;
    return p;
}
static int mp_pair_player(int slot) {
    return mp_roster_player(r32(mp_addr(0x2e0bc,0x2de94)+4*slot),1);
}
static unsigned mp_player_bit(int player) { return player < 0 ? 0 : 1u << player; }
static void mp_campaign_clear(void) {
    g_mp_campaign = g_mp_restore_pending = 0;
    g_mp_context = MP_CAM_NONE;
    g_mp_ui_player = -1;
    g_mp_combatants = 0;
}
/* Retract only host-produced keys. A handoff must not retry the former player's
 * End Turn or inventory request on the next player; guest-generated keys remain. */
static void mp_campaign_clear_input(void) {
    uint32_t raw = mp_addr(0x3bf74,0x3bc5c);
    uint16_t key = r16(raw);
    if ((g_rest_pending && key == 0x12) || (g_inv_pending && key == 0x39)
        || (g_quest_quit_pending && key == 0x10) || (g_popup_injected && key == g_popup_injected))
        w16(raw,0);
    g_rest_pending = g_inv_pending = g_rest_tries = g_inv_tries = 0;
    g_quest_quit_pending = g_popup_injected = 0;
    numbered_menu_reset();
    mp_clear_host_input();
}
static void mp_campaign_context(MpCampaignContext context, int owner, unsigned fighters) {
    int players = mp_campaign_players();
    if (!g_sdl_mode || g_mp_practice || !players) return;
    unsigned required = context == MP_CAM_COMBAT ? fighters : mp_player_bit(owner);
    int first = !g_mp_campaign;
    if (!first && context == g_mp_context && owner == g_mp_ui_player
        && fighters == g_mp_combatants) return;
    int changed = first || owner != g_mp_ui_player || required != g_mp.required
        || (context==MP_CAM_MAP && g_mp_context!=MP_CAM_MAP);
    int previous_owner = g_mp_ui_player;
    int winner = context == MP_CAM_UI && g_mp_context == MP_CAM_COMBAT
        && (g_mp_combatants & mp_player_bit(owner));
    g_mp_campaign = 1;
    g_mp_context = context;
    g_mp_ui_player = owner;
    g_mp_combatants = fighters;
    if (first && (g_mp.phase == MP_OFF || g_mp.players != players)) {
        int pads = mp_pad_count();
        int capacity = pads < 2 ? pads + 1 : pads;
        mp_begin_campaign(&g_mp,players,capacity < players,required);
    } else {
        g_mp.required = required;
        if (changed) mp_use_choices(&g_mp,owner,context==MP_CAM_COMBAT || winner
                                   || (first && context==MP_CAM_UI));
        /* An assigned pad identifies the player, but does not acknowledge that
         * it is their turn to select a knight. Keep this handoff visible until
         * that player presses Start (or Enter for the assigned keyboard).
         * Shared devices already wait in MP_CLAIM; warm restores keep ownership. */
        if (!first && owner>=0 && owner!=previous_owner && g_mp.device[owner]!=MP_NONE
            && context==MP_CAM_SELECT) {
            g_mp.claim=owner;
            g_mp.phase=MP_CONFIRM;
        }
    }
    if (!required) g_mp.phase=MP_PLAY; /* original AI turns run freely */
    if (changed) mp_campaign_clear_input();
    if (g_log && changed) {
        fprintf(g_log,"MP-CAMPAIGN context=%d owner=P%d required=%x shared=%d players=%d phase=%d scene=%u pair=%x,%x\n",
                context,owner+1,g_mp.required,g_mp.shared,players,g_mp.phase,
                r32(mp_addr(0x29f16,0x29dca)),r32(mp_addr(0x2e0bc,0x2de94)),r32(mp_addr(0x2e0c0,0x2de98)));
        fflush(g_log);
    }
}
static void mp_campaign_selection(void) {
    int owner=mp_roster_player(r32(mp_addr(0x306e8,0x3049c)),0);
    if (owner >= 0) mp_campaign_context(MP_CAM_SELECT,owner,0);
}
static void mp_campaign_combat(void) {
    /* The native monster constructors replace pair[0] only. pair[1] can still
     * name an unrelated human from selection or an earlier encounter. Original
     * combat code (2191c / retail21920) reads it only for scenes12 and16. */
    unsigned scene=r32(mp_addr(0x29f16,0x29dca));
    int a=mp_pair_player(0),b=(scene==12 || scene==16) ? mp_pair_player(1) : -1;
    mp_campaign_context(MP_CAM_COMBAT,a >= 0 ? a : b,mp_player_bit(a)|mp_player_bit(b));
}
static void mp_campaign_ui(void) {
    int owner=mp_pair_player(0);
    if (owner < 0) owner=0; /* original global acknowledgements, e.g. daybreak */
    mp_campaign_context(MP_CAM_UI,owner,0);
}
/* Only used when loading: inspect the live call stack for an original JSR return.
 * This recognizes nested loading, pause and rendering inside a combat scene. */
static int mp_guest_call_active(uint32_t target) {
    uint32_t pc=m68k_get_reg(NULL,M68K_REG_PC),sp=m68k_get_reg(NULL,M68K_REG_SP);
    if (pc == target) return 1;
    if (sp > RAM_SIZE-4) return 0;
    uint32_t end=sp+2048 < RAM_SIZE-4 ? sp+2048 : RAM_SIZE-4;
    for (uint32_t at=sp; at<=end; at+=2) {
        uint32_t ret=r32(at);
        if (ret >= 0x21006 && ret < 0x44000 && !(ret&1)
            && r16(ret-6) == 0x4eb9 && r32(ret-4) == target) return 1;
    }
    return 0;
}
static void mp_campaign_restore(void) {
    if (!g_mp_restore_pending) return;
    g_mp_restore_pending=0;
    if (!mp_campaign_players() || mp_guest_call_active(mp_addr(0x228de,0x22880))) {
        mp_reset(&g_mp); return;
    }
    if (mp_guest_call_active(mp_addr(0x22cdc,0x22c82))
        && r16(mp_addr(0x306ec,0x304a0))) mp_campaign_selection();
    else if (numbered_menu_wait_for_pc(m68k_get_reg(NULL,M68K_REG_PC))) {
        int owner=mp_roster_player(r32(mp_addr(0x2ebd0,0x2e9ac)),1);
        mp_campaign_context(MP_CAM_MAP,owner,0);
    }
    else if (mp_guest_call_active(mp_addr(0x2173c,0x21776))) mp_campaign_combat();
    else mp_campaign_ui();
    mp_campaign_clear_input();
}
static int mp_keyboard_gameplay(void) {
    return !mp_active() || (g_mp_campaign && g_mp_ui_player >= 0
        && g_mp.device[g_mp_ui_player] == MP_KEYBOARD && g_mp.phase == MP_PLAY);
}
static void mp_campaign_hook(unsigned pc) {
    if (!g_sdl_mode || g_mp_practice) return;
    if (pc == mp_addr(0x22d54,0x22cfa) && r16(pc)==0x4eb9
        && r32(pc+2)==mp_addr(0x22fe6,0x22f92)) mp_campaign_selection();
    else if ((pc == mp_addr(0x40188,0x3fe6e) && r16(pc)==0x2079)
          || (pc == mp_addr(0x40204,0x3fed0) && r16(pc)==0x4eb9)) {
        int owner=mp_roster_player(r32(mp_addr(0x2ebd0,0x2e9ac)),1);
        mp_campaign_context(MP_CAM_MAP,owner,0);
    } else if ((pc == mp_addr(0x2173c,0x21776) && r16(pc)==(g_lineage==LIN_RETAIL?0x33fc:0x4279))
            || (pc == mp_addr(0x22fc4,0x22f70) && r16(pc)==0x0c28)
            || (pc == mp_addr(0x21aa4,0x21aa8) && r16(pc)==0x4a79)) mp_campaign_combat();
    else if ((pc == mp_addr(0x2bbec,0x2bac4) && r16(pc)==(g_lineage==LIN_RETAIL?0x23c0:0x33fc))
          || (pc == mp_addr(0x2d61c,0x2d3ea) && r16(pc)==(g_lineage==LIN_RETAIL?0x48e7:0x33fc)))
        mp_campaign_ui();
    else if (g_mp_context==MP_CAM_MAP && pc==mp_addr(0x22fd0,0x22f7c)
             && r16(pc)==0x6100 && !(g_autoswap && g_autoswap_armed))
        mp_campaign_ui(); /* daybreak can follow an AI turn; disk swaps have no UI owner */
    /* Every non-actor reader belongs to the current UI player. In particular,
     * the original pointer task selects port 0 for a duel's winning defender. */
    if (g_mp_campaign && pc == mp_addr(0x2305a,0x23006) && r16(pc)==0x4e75
        && r16(pc-6)==0x3239 && r32(pc-4)==mp_addr(0x2ebc6,0x2e9a2)) {
        unsigned input=g_mp.phase==MP_PLAY && g_mp_ui_player>=0 ? g_mp_input[g_mp_ui_player] : 0;
        m68k_set_reg(M68K_REG_D0,(m68k_get_reg(NULL,M68K_REG_D0)&0xffff0000u)|input);
        m68k_set_reg(M68K_REG_D1,(m68k_get_reg(NULL,M68K_REG_D1)&0xffff0000u)|input);
    }
}
