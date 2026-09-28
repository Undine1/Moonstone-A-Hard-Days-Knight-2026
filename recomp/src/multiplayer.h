/* Host device ownership only. Original game code still owns all gameplay.
 * Session assignments are deliberately absent from MOONSAVE files. */
#ifndef MOON_MULTIPLAYER_H
#define MOON_MULTIPLAYER_H

#define MP_NONE (-1)
#define MP_KEYBOARD (-2)
#define MP_MAX_PLAYERS 4
/* MP_RELEASE consumes an accepted identification press only, never gameplay. */
typedef enum { MP_OFF, MP_CLAIM, MP_RELEASE, MP_PLAY, MP_CONFIRM } MpPhase;
typedef struct {
    MpPhase phase;
    int players;
    int device[MP_MAX_PLAYERS];   /* SDL connection instance IDs, not indices */
    int chosen[MP_MAX_PLAYERS];   /* session choices survive shared turn transfers */
    unsigned disconnected;      /* chosen pad was removed; requires fresh Start */
    int claim;                    /* claiming/confirming/accepted slot, never renumber */
    int recovery;                 /* suppress Practice auto-assignment after loss */
    unsigned required;            /* players needed at this original game boundary */
    int shared;                   /* device handoffs; finalized after initial choices */
    unsigned enrolled;            /* chosen device type survives transfers/disconnects */
    int shared_used;              /* a device was explicitly shared during setup */
} MpSession;

static void mp_reset(MpSession *s) {
    *s = (MpSession){.phase = MP_OFF};
    for (int p = 0; p < MP_MAX_PLAYERS; p++) s->device[p] = s->chosen[p] = MP_NONE;
}
static void mp_begin(MpSession *s) {
    mp_reset(s);
    s->players = 2;
    s->required = 3;
    s->phase = MP_CLAIM;
}
static int mp_owner(const MpSession *s, int device) {
    if (device == MP_NONE) return -1;
    for (int p = 0; p < s->players; p++) if (s->device[p] == device) return p;
    return -1;
}
static void mp_next(MpSession *s) {
    for (int p = 0; p < s->players; p++) if ((s->required & (1u << p)) && s->device[p] == MP_NONE) {
        s->claim = p;
        s->phase = MP_CLAIM;
        return;
    }
    s->phase = MP_PLAY;
}
static void mp_begin_campaign(MpSession *s, int players, int shared, unsigned required) {
    mp_reset(s);
    s->players = players;
    s->shared = shared;
    s->required = required;        /* identify players when their original turn needs them */
    mp_next(s);
}
/* Restore only known, connected choices. A shared device can move from an idle
 * player, but never from another required fighter. Temporary duel assignments
 * remain in device[] until that fight/loot ends; chosen[] remains unchanged. */
static void mp_use_choices(MpSession *s, int owner, int combat) {
    /* A map turn with no human owner ends the loan too; an AI may immediately
     * attack a human before another human map turn restores their choices. */
    if (!combat && (owner >= 0 || !s->required)) for (int p = 0; p < s->players; p++)
        if (s->chosen[p] != MP_NONE && s->device[p] != s->chosen[p]) s->device[p] = MP_NONE;
    for (int p = 0; p < s->players; p++) {
        if (!(s->required & (1u << p)) || s->device[p] != MP_NONE
            || s->chosen[p] == MP_NONE || (s->disconnected & (1u << p))) continue;
        int former = mp_owner(s,s->chosen[p]);
        if (former >= 0 && (s->required & (1u << former))) continue;
        if (former >= 0) s->device[former] = MP_NONE;
        s->device[p] = s->chosen[p];
    }
    mp_next(s);
}
/* Shared devices may pass between turns, but two simultaneous fighters must
 * always own distinct devices. Temporary loans preserve normal assignments. */
static int mp_can_claim(const MpSession *s, int device) {
    int owner = mp_owner(s, device);
    if (device == MP_KEYBOARD) {
        /* Campaign players may explicitly share the keyboard between turns.
         * A required player's keyboard stays reserved even without an active
         * binding. Later loans cannot recover a lost pad or change choices. */
        unsigned player = 1u << s->claim;
        int first = !(s->enrolled & player);
        if (owner >= 0 && (!s->shared || (s->required & (1u << owner)))) return 0;
        for (int p = 0; p < s->players; p++)
            if (s->chosen[p] == MP_KEYBOARD && (!s->shared || (s->required & (1u << p)))) return 0;
        if (first) return 1;
        if (!s->shared || !(s->required & player) || (s->disconnected & player)
            || s->chosen[s->claim] < 0) return 0;
        int other = mp_owner(s,s->chosen[s->claim]);
        return other >= 0 && other != s->claim && (s->required & (1u << other))
            && s->chosen[other] == s->chosen[s->claim];
    }
    /* Recovery replaces the missing pad for its whole remembered group.
     * Taking another group's pad here would merge the groups permanently,
     * including idle players with no active device[] binding. Initial sharing
     * and temporary duel loans remain separate from this recovery path. */
    if (s->disconnected & (1u << s->claim))
        for (int p = 0; p < s->players; p++)
            if (s->chosen[p] == device && s->chosen[p] != s->chosen[s->claim]) return 0;
    return device != MP_NONE && (owner < 0 || (s->shared && !(s->required & (1u << owner))));
}
static int mp_claim(MpSession *s, int device) {
    if (s->phase != MP_CLAIM || !mp_can_claim(s, device))
        return 0;
    int former = mp_owner(s, device);
    int first = !(s->enrolled & (1u << s->claim));
    if (former >= 0) s->shared_used = 1;
    if (former >= 0) s->device[former] = MP_NONE;
    s->device[s->claim] = device;
    if (first) {
        /* A remembered choice still counts if its active binding was cleared
         * by a load or a turn boundary during enrollment. */
        for (int p = 0; p < s->players; p++)
            if (p != s->claim && s->chosen[p] == device) s->shared_used = 1;
        s->chosen[s->claim] = device;
    }
    else if (s->disconnected & (1u << s->claim)) {
        /* One explicit reconnect restores the shared group's choice. Other
         * players do not need to identify the same physical pad again. */
        int old = s->chosen[s->claim];
        for (int p = 0; p < s->players; p++) if (s->chosen[p] == old) {
            s->chosen[p] = device;
            s->disconnected &= ~(1u << p);
        }
    }
    s->enrolled |= 1u << s->claim;
    /* Three players choosing two pads plus keyboard have personal devices.
     * Choosing to share any device retains shared turns. Decide once, after
     * everyone has chosen, so reconnects/hotplug cannot change sharing mode. */
    if (first && s->enrolled == (1u << s->players)-1 && !s->shared_used)
        s->shared = 0;
    mp_next(s);
    if (s->phase == MP_PLAY) s->phase = MP_RELEASE;
    return 1;
}
static void mp_removed(MpSession *s, int device) {
    if (device < 0) return;
    for (int p = 0; p < s->players; p++) if (s->chosen[p] == device)
        s->disconnected |= 1u << p;
    int p = mp_owner(s, device);
    if (p < 0) return;            /* unrelated removal cannot change ownership */
    s->device[p] = MP_NONE;
    s->recovery = 1;
    if (s->required & (1u << p)) mp_next(s);
}
#endif
