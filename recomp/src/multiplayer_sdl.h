/* Included by moon.c after controls and ControllerNotice are defined. */
#define MP_MAX_PADS 8
typedef struct {
    SDL_GameController *handle;
    SDL_JoystickID id;
    int now[CONTROL_COUNT], edge[CONTROL_COUNT];
    int start, start_edge, solo, solo_edge;
} MpPad;
static MpPad g_mp_pads[MP_MAX_PADS];
static SDL_JoystickID g_primary_pad = MP_NONE;
static void host_draw_text(SDL_Renderer *, const char *, int, int);
static void controller_notice_show(ControllerNotice *, SDL_GameController *);

static MpPad *mp_pad(int id) {
    for (int i = 0; i < MP_MAX_PADS; i++)
        if (g_mp_pads[i].handle && g_mp_pads[i].id == id) return &g_mp_pads[i];
    return NULL;
}
static int mp_pad_count(void) {
    int n = 0;
    for (int i = 0; i < MP_MAX_PADS; i++) n += g_mp_pads[i].handle != NULL;
    return n;
}
/* A cold save loads before SDL opens devices. Also handle a controller added
 * during keyboard-only Practice. Campaign settings never gate either fighter. */
static void mp_try_begin_practice(void) {
    if (g_mp_practice || !mp_pad_count() || !mp_practice_snapshot()) return;
    g_mp_practice = 1;
    mp_begin_practice();
}
static void controller_set_primary(SDL_JoystickID id, const char *reason) {
    if (g_primary_pad == id) return;
    g_primary_pad = id;
    MpPad *pad = mp_pad(id);
    const char *name = pad ? SDL_GameControllerName(pad->handle) : NULL;
    if (g_log) {
        fprintf(g_log,"CONTROLLER-PRIMARY id=%d reason=%s device=%s\n",id,reason,
                pad ? (name ? name : "unnamed controller") : "awaiting input");
        fflush(g_log);
    }
}
static MpPad *mp_open(int index) {
    if (!SDL_IsGameController(index)) return NULL;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    if (id < 0) return NULL;
    MpPad *existing = mp_pad(id);
    if (existing) return existing;
    for (int i = 0; i < MP_MAX_PADS; i++) if (!g_mp_pads[i].handle) {
        SDL_GameController *handle = SDL_GameControllerOpen(index);
        if (!handle) {
            if (g_log) fprintf(g_log, "MP-DEVICE open failed: %s\n", SDL_GetError());
            return NULL;
        }
        g_mp_pads[i] = (MpPad){.handle = handle, .id = id};
        /* A button already held at connection is not a fresh claim/action. */
        for (int a = 0; a < CONTROL_COUNT; a++)
            g_mp_pads[i].now[a] = control_pad_strength(handle,(ControlAction)a) > 0;
        g_mp_pads[i].start = SDL_GameControllerGetButton(handle,SDL_CONTROLLER_BUTTON_START);
        g_mp_pads[i].solo = SDL_GameControllerGetButton(handle,SDL_CONTROLLER_BUTTON_A);
        if (g_log) fprintf(g_log,"MP-DEVICE opened index=%d id=%d device=%s\n",index,id,
                           SDL_GameControllerName(handle) ? SDL_GameControllerName(handle) : "unnamed controller");
        return &g_mp_pads[i];
    }
    return NULL;
}
static void controller_open_all(ControllerNotice *notice) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        int known = mp_pad(SDL_JoystickGetDeviceInstanceID(i)) != NULL;
        MpPad *pad = mp_open(i);
        if (pad && !known && notice) controller_notice_show(notice,pad->handle);
    }
    mp_try_begin_practice();
    mp_campaign_restore();
}
static void controller_device_event(ControllerNotice *notice,
                                    const SDL_ControllerDeviceEvent *event) {
    if (event->type == SDL_CONTROLLERDEVICEADDED) {
        int known = mp_pad(SDL_JoystickGetDeviceInstanceID(event->which)) != NULL;
        MpPad *pad = mp_open(event->which);
        if (pad && !known) {
            controller_notice_show(notice, pad->handle);
            mp_try_begin_practice();
        }
    } else if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        MpPad *pad = mp_pad(event->which);
        if (!pad) return;
        int owner = mp_owner(&g_mp, pad->id);
        if (g_log) fprintf(g_log, "MP-DEVICE removed id=%d player=%d\n",pad->id,owner+1);
        mp_removed(&g_mp, pad->id);
        if (g_primary_pad == pad->id) controller_set_primary(MP_NONE,"disconnected");
        SDL_GameControllerClose(pad->handle);
        *pad = (MpPad){0};
        controller_notice_show(notice, NULL);
        if (mp_active()) mp_clear_host_input();
    }
}
static void mp_poll_pads(void) {
    for (int i = 0; i < MP_MAX_PADS; i++) {
        MpPad *pad = &g_mp_pads[i];
        if (!pad->handle) continue;
        for (int a = 0; a < CONTROL_COUNT; a++) {
            int down = control_pad_strength(pad->handle, (ControlAction)a) > 0;
            pad->edge[a] = down && !pad->now[a];
            pad->now[a] = down;
        }
        int start = SDL_GameControllerGetButton(pad->handle, SDL_CONTROLLER_BUTTON_START);
        int solo = SDL_GameControllerGetButton(pad->handle, SDL_CONTROLLER_BUTTON_A);
        pad->start_edge = start && !pad->start;
        pad->solo_edge = solo && !pad->solo;
        pad->start = start;
        pad->solo = solo;
    }
}
/* SDL's startup enumeration is not connection chronology. The first fresh
 * configured input chooses the menu/single-player pad for this run. Hotplug and
 * other pads' input never steal it. Practice inherits it for P1; an unknown
 * owner or a disconnected player still identifies their controller with Start.
 * Keep polling every pad even in the background/Practice, so returning to menus
 * cannot turn another device's held shortcut into a new press. */
static MpPad *controller_primary(int focused) {
    if (!focused || mp_active() || g_mp_restore_pending) return NULL;
    MpPad *owner = mp_pad(g_primary_pad);
    if (owner) return owner;
    for (int i = 0; i < MP_MAX_PADS; i++) {
        MpPad *pad = &g_mp_pads[i];
        if (!pad->handle) continue;
        int fresh = pad->start_edge || pad->solo_edge;
        for (int a = 0; a < CONTROL_COUNT; a++) fresh |= pad->edge[a];
        if (fresh) {
            controller_set_primary(pad->id,"first input");
            return pad;
        }
    }
    return NULL;
}
static uint16_t mp_action_word(const int *down) {
    return (uint16_t)((down[CONTROL_RIGHT] ? 1 : 0) | (down[CONTROL_LEFT] ? 2 : 0)
           | (down[CONTROL_DOWN] ? 4 : 0) | (down[CONTROL_UP] ? 8 : 0)
           | (down[CONTROL_FIRE] ? 16 : 0));
}
static void mp_clear_host_input(void) {
    memset(g_mp_input,0,sizeof(g_mp_input));
    g_ji_up = g_ji_dn = g_ji_lf = g_ji_rt = g_fire = g_fire2 = 0;
    g_mouse_dx = g_mouse_dy = g_rmb = g_kdigit = 0;
    g_pause_request = g_rest_request = g_inv_request = 0;
    g_inventory_close_request = 0;
    g_quest_quit_request = g_ver_request = 0;
    g_keyq_head = g_keyq_tail = 0;
}
static void mp_log_assignment(void) {
    if (!g_log) return;
    fprintf(g_log, "MP-ASSIGN phase=%d players=%d P1=%d P2=%d P3=%d P4=%d required=%x shared=%d\n",
            g_mp.phase, g_mp.players, g_mp.device[0], g_mp.device[1],
            g_mp.device[2],g_mp.device[3],g_mp.required,g_mp.shared);
    for (int p = 0; p < g_mp.players; p++) {
        MpPad *pad = mp_pad(g_mp.device[p]);
        const char *name = pad ? SDL_GameControllerName(pad->handle) : NULL;
        fprintf(g_log, "MP-PLAYER %d %s device=%s chosen=%d disconnected=%d\n", p+1, g_mp_campaign ? "campaign" : p ? "GREEN" : "BLUE",
                pad ? (name ? name : "unnamed controller") :
                g_mp.device[p] == MP_KEYBOARD ? "keyboard" : "unassigned",
                g_mp.chosen[p],!!(g_mp.disconnected & (1u<<p)));
    }
    fflush(g_log);
}
static int mp_keyboard_claimable(void) {
    return g_mp_campaign && g_mp.phase==MP_CLAIM && mp_can_claim(&g_mp,MP_KEYBOARD);
}
/* Initial Practice assignment only. Keep registry order for the next available
 * pad, with keyboard used only when P1 is the sole connected controller.
 * Disconnect recovery never runs this selection or replaces a keyboard owner. */
static void mp_assign_practice_second(void) {
    if (!g_mp_practice || g_mp.recovery || g_mp.players != 2
        || g_mp.phase != MP_CLAIM || g_mp.claim != 1
        || g_mp.device[0] < 0 || g_mp.device[1] != MP_NONE) return;
    int second = MP_KEYBOARD;
    for (int i = 0; i < MP_MAX_PADS; i++)
        if (g_mp_pads[i].handle && g_mp_pads[i].id != g_mp.device[0]) {
            second = g_mp_pads[i].id;
            break;
        }
    mp_claim(&g_mp,second);
    g_mp.claim = 0; /* Only P1 can have pressed Start during initial setup. */
}
static void mp_begin_practice(void) {
    mp_begin(&g_mp);
    if (mp_pad(g_primary_pad)) {
        mp_claim(&g_mp,g_primary_pad);
        mp_assign_practice_second();
        /* There was no identification press to consume: the menu owner is
         * already known, and ordinary held gameplay input remains usable. */
        g_mp.phase = MP_PLAY;
    }
    mp_clear_host_input();
    if (g_log) fprintf(g_log,"MP-PRACTICE %s\n",g_mp.phase == MP_PLAY
                       ? "inherited menu controller; assigned Player 2 automatically"
                       : "no menu controller; awaiting Player 1 Start");
    mp_log_assignment();
}
/* keyboard_claim is a fresh Return press while an assignment is pending. */
static int mp_update(const Uint8 *keyboard, int keyboard_claim, int focused, int mouse_fire,
                     int *do_save, int *do_load, int *running) {
    MpSession before = g_mp;
    if (focused && g_mp.phase == MP_CONFIRM) {
        int device = g_mp.device[g_mp.claim];
        MpPad *pad = mp_pad(device);
        if ((pad && pad->start_edge) || (device == MP_KEYBOARD && keyboard_claim))
            g_mp.phase = MP_RELEASE;
    }
    if (focused && g_mp.phase == MP_CLAIM) {
        int claimed = 0;
        for (int i = 0; i < MP_MAX_PADS; i++) if (g_mp_pads[i].handle
            && g_mp_pads[i].start_edge && mp_claim(&g_mp, g_mp_pads[i].id)) {
            mp_assign_practice_second();
            claimed = 1;
            break;
        }
        /* One visible player's choice per poll; simultaneous Enter/Start must
         * not accept the next player's prompt before it has appeared. */
        if (!claimed && keyboard_claim && mp_keyboard_claimable()) mp_claim(&g_mp, MP_KEYBOARD);
    }
    int keys[CONTROL_COUNT];
    for (int a = 0; a < CONTROL_COUNT; a++)
        keys[a] = control_keyboard_down((ControlAction)a, keyboard);
    if (g_name_entry_live && (g_cur_frame-g_name_entry_live)<3)
        keys[CONTROL_FIRE]=control_keyboard_down_without_name_text(CONTROL_FIRE,keyboard);
    /* Consume only the Start/Enter accepted by player identification, so it
     * cannot also pause combat or confirm a knight. Ordinary scene changes,
     * held gameplay controls, focus return and warm loads need no release gate. */
    if (focused && g_mp.phase == MP_RELEASE) {
        int device = g_mp.device[g_mp.claim];
        MpPad *pad = mp_pad(device);
        int held = device == MP_KEYBOARD
            ? keyboard[SDL_SCANCODE_RETURN] || keyboard[SDL_SCANCODE_KP_ENTER]
            : pad && pad->start;
        if (!held) g_mp.phase = MP_PLAY;
    }
    if (before.phase != g_mp.phase || before.players != g_mp.players
        || memcmp(before.device,g_mp.device,sizeof(g_mp.device))) {
        if (before.chosen[0] != g_mp.chosen[0] && g_mp.chosen[0] != MP_NONE)
            controller_set_primary(g_mp.chosen[0] >= 0 ? g_mp.chosen[0] : MP_NONE,
                                   g_mp_campaign ? "Campaign P1" : "Practice P1");
        mp_clear_host_input();
        mp_log_assignment();
    }
    if (g_mp.phase != MP_PLAY || !focused) {
        mp_clear_host_input();
        return 1;
    }
    if (g_mp_campaign) g_mp.required=g_mp_context==MP_CAM_COMBAT
        ? g_mp_combatants : mp_player_bit(g_mp_ui_player);
    for (int p = 0; p < MP_MAX_PLAYERS; p++) {
        g_mp_input[p] = 0;
        if (p >= g_mp.players || !(g_mp.required & (1u<<p))) continue;
        MpPad *pad = mp_pad(g_mp.device[p]);
        if (pad) {
            g_mp_input[p] = mp_action_word(pad->now);
            /* Practice is already an explicit scope. Its original pause poll
             * need not run every host frame: retain the edge until that poll. */
            if (pad->edge[CONTROL_PAUSE] && (g_mp_practice || g_mp_context==MP_CAM_COMBAT)) g_pause_request = 1;
            if (pad->edge[CONTROL_QUICKSAVE]) *do_save = 1;
            if (pad->edge[CONTROL_QUICKLOAD]) *do_load = 1;
            if (pad->edge[CONTROL_QUIT]) *running = 0;
            if (g_mp_campaign && p==g_mp_ui_player && g_inventory_menu_active
                && pad->edge[CONTROL_CLOSE_INVENTORY]) g_inventory_close_request=1;
            if (g_mp_campaign && p==g_mp_ui_player && g_mp_context==MP_CAM_MAP
                && !g_in_inventory && g_map_live && (g_cur_frame-g_map_live)<3) {
                if (pad->edge[CONTROL_INVENTORY]) g_inv_request=1;
                if (pad->edge[CONTROL_END_TURN]) g_rest_request=1;
                if (pad->edge[CONTROL_ABANDON_QUEST]) g_quest_quit_request=1;
                if (pad->edge[CONTROL_SHOW_VERSION] && g_retail_parity) g_ver_request=1;
            }
        } else if (g_mp.device[p] == MP_KEYBOARD) {
            g_mp_input[p] = mp_action_word(keys) | (mouse_fire ? 16 : 0);
        }
    }
    /* Original shared Practice continue/menu polls retain player one's device. */
    int owner=g_mp_campaign ? g_mp_ui_player : 0;
    unsigned input = owner>=0 ? g_mp_input[owner] : 0;
    g_ji_rt = !!(input & 1); g_ji_lf = !!(input & 2);
    g_ji_dn = !!(input & 4); g_ji_up = !!(input & 8);
    g_fire = g_fire2 = !!(input & 16);
    if (!(g_mp_campaign && owner>=0 && g_mp.device[owner]==MP_KEYBOARD)) {
        g_mouse_dx = 6 * (g_ji_rt - g_ji_lf);
        g_mouse_dy = 6 * (g_ji_dn - g_ji_up);
        g_rmb = 0;
    }
    return 0;
}
static const char *mp_prompt(int focused, char text[64]) {
    if (!mp_active() || g_mp.players < 2 || g_mp.phase == MP_OFF) return NULL;
    if (!focused) return "PAUSED";
    if (g_mp.phase == MP_PLAY || g_mp.phase == MP_RELEASE) return NULL;
    if (g_mp.phase == MP_CONFIRM) {
        snprintf(text,64,"PLAYER %d PRESS %s",g_mp.claim+1,
                 g_mp.device[g_mp.claim] == MP_KEYBOARD ? "ENTER" : "START");
        return text;
    }
    int available = 0;
    for (int i = 0; i < MP_MAX_PADS; i++)
        if (g_mp_pads[i].handle && mp_can_claim(&g_mp,g_mp_pads[i].id)) available++;
    int keyboard = mp_keyboard_claimable();
    const char *action = available ? (keyboard ? "PRESS START OR ENTER" : "PRESS START")
                                  : (keyboard ? "PRESS ENTER" : "CONNECT CONTROLLER");
    snprintf(text,64,"PLAYER %d %s",g_mp.claim+1,action);
    return text;
}
static void mp_overlay(SDL_Renderer *ren, int focused) {
    char text[64];
    const char *prompt = mp_prompt(focused,text);
    if (!prompt) return;
    Uint8 r,g,b,a; SDL_BlendMode blend;
    SDL_GetRenderDrawColor(ren,&r,&g,&b,&a); SDL_GetRenderDrawBlendMode(ren,&blend);
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
    int text_width = (int)strlen(prompt)*6-1;
    SDL_Rect box = {(320-text_width-24)/2,112,text_width+24,31};
    SDL_SetRenderDrawColor(ren,10,15,24,235);
    SDL_RenderFillRect(ren,&box);
    SDL_SetRenderDrawColor(ren,238,240,244,255);
    host_draw_text(ren,prompt,box.x+12,box.y+12);
    SDL_SetRenderDrawColor(ren,r,g,b,a); SDL_SetRenderDrawBlendMode(ren,blend);
}
static void mp_close_pads(void) {
    for (int i = 0; i < MP_MAX_PADS; i++) {
        if (g_mp_pads[i].handle) SDL_GameControllerClose(g_mp_pads[i].handle);
        g_mp_pads[i] = (MpPad){0};
    }
    g_primary_pad = MP_NONE;
}
