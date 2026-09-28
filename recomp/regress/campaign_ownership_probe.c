/* Test-only original combat-scene oracle and campaign device lifecycle checks.
 * Never distribute. Every invocation requires an explicit scratch --log. */
#define main moonstone_main
#define moon_instr_hook production_hook
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int oracle,returned,second_read;
void moon_instr_hook(unsigned pc) {
    if (!oracle) { production_hook(pc); return; }
    if (pc==mp_addr(0x21946,0x2194a)) second_read=1;
    if (pc==0x1ef000) { returned=1;m68k_end_timeslice(); }
}
static int original_second_slot(void) {
    oracle=1;returned=second_read=0;
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,0x1ee000);
    w32(0x1ee000,0x1ef000);w16(0x1ef000,0x4e71);
    w32(mp_addr(0x2defe,0x2dcd6),0);
    m68k_set_reg(M68K_REG_PC,mp_addr(0x2191c,0x21920));
    m68k_execute(2000);assert(returned);oracle=0;
    return second_read;
}
static void recognition(int players) {
    g_os=g_sdl_mode=1;g_mp_practice=0;
    w16(mp_addr(0x2e024,0x2ddfc),(uint16_t)players);
    w16(mp_addr(0x3051e,0x302d2),3);
    unsigned base=mp_addr(0x2e7dc,0x2e5b4);
    for(int p=0;p<4;p++) {
        w32(base+p*0x84+0x36,p<players?3-p:4);
        w16(base+p*0x84+0x50,20);
    }
    assert(mp_campaign_players()==players);
}
static void combat_oracle(void) {
    unsigned base=mp_addr(0x2e7dc,0x2e5b4),pair=mp_addr(0x2e0bc,0x2de94);
    unsigned cases=0;
    recognition(4);
    for(unsigned scene=0;scene<48;scene++)for(int a=0;a<4;a++)for(int b=0;b<4;b++)if(a!=b) {
        w32(pair,base+a*0x84);w32(pair+4,base+b*0x84);
        w32(mp_addr(0x29f16,0x29dca),scene);
        unsigned expected=(1u<<a)|(original_second_slot()?(1u<<b):0);
        mp_begin_campaign(&g_mp,4,0,1u<<a);
        for(int p=0;p<4;p++)g_mp.device[p]=100+p;
        g_mp.enrolled=15;g_mp.phase=MP_PLAY;
        g_mp_campaign=1;g_mp_context=MP_CAM_MAP;g_mp_ui_player=a;g_mp_combatants=0;
        mp_campaign_combat();
        assert(g_mp.required==expected && g_mp_ui_player==a && g_mp.phase==MP_PLAY);
        cases++;
    }
    /* AI attackers and defenders retain exactly the one participating human. */
    recognition(3);w32(mp_addr(0x29f16,0x29dca),12);
    for(int p=0;p<3;p++)for(int reverse=0;reverse<2;reverse++) {
        w32(pair,base+(reverse?3:p)*0x84);w32(pair+4,base+(reverse?p:3)*0x84);
        mp_reset(&g_mp);g_mp.players=3;g_mp.phase=MP_PLAY;g_mp.device[p]=100+p;
        g_mp_campaign=1;g_mp_context=MP_CAM_MAP;g_mp_ui_player=p;g_mp_combatants=0;
        mp_campaign_combat();assert(g_mp.required==1u<<p && g_mp_ui_player==p);cases++;
    }
    printf("PASS: %u original %s scene/participant oracle cases, including all four human owners and both AI roles\n",
           cases,g_lineage==LIN_RETAIL?"retail":"port");
}
static void choose(MpSession *s,int p,int device) {
    s->required=1u<<p;mp_next(s);assert(mp_claim(s,device));s->phase=MP_PLAY;
}
static void choices(void) {
    unsigned sessions=0,turns=0,duels=0,keyboards=0;
    for(int players=2;players<=4;players++)for(int pads=1;pads<=4;pads++)
    for(int keyboard=-1;keyboard<players;keyboard++) {
        MpSession s;int chosen[4];
        mp_begin_campaign(&s,players,1,1);
        for(int p=0;p<players;p++) {
            chosen[p]=p==keyboard?MP_KEYBOARD:100+p%pads;choose(&s,p,chosen[p]);
        }
        for(int round=0;round<4;round++)for(int p=0;p<players;p++) {
            s.required=1u<<p;mp_use_choices(&s,p,0);
            assert(s.phase==MP_PLAY && s.device[p]==chosen[p]);
            assert(!memcmp(s.chosen,chosen,players*sizeof(int)));turns++;
        }
        for(int a=0;a<players;a++)for(int b=0;b<players;b++)if(a!=b) {
            s.required=1u<<a;mp_use_choices(&s,a,0);
            s.required=(1u<<a)|(1u<<b);mp_use_choices(&s,a,1);
            if(chosen[a]==chosen[b]) {
                assert(s.phase==MP_CLAIM && s.claim==b);
                assert(!mp_claim(&s,chosen[a]));
                MpSession keys=s;
                assert(mp_claim(&keys,MP_KEYBOARD)==(keyboard<0));
                if(keyboard<0) {
                    assert(keys.device[b]==MP_KEYBOARD && keys.device[a]==chosen[a]);
                    assert(!memcmp(keys.chosen,chosen,players*sizeof(int)));
                    for(int winner=0;winner<2;winner++) {
                        MpSession loot=keys;int p=winner?b:a,device=loot.device[p];
                        loot.required=1u<<p;mp_use_choices(&loot,p,1);
                        assert(loot.phase==MP_PLAY && loot.device[p]==device);
                        MpSession ai=loot;ai.required=0;mp_use_choices(&ai,-1,0);
                        assert(ai.phase==MP_PLAY && mp_owner(&ai,MP_KEYBOARD)<0);
                        ai.required=1u<<p;mp_use_choices(&ai,p,1);
                        assert(ai.phase==MP_PLAY && ai.device[p]==chosen[p]);
                        mp_use_choices(&loot,p,0);
                        assert(loot.phase==MP_PLAY && loot.device[p]==chosen[p]);
                        assert(mp_owner(&loot,MP_KEYBOARD)<0);
                    }
                    keyboards++;
                }
                assert(mp_claim(&s,200));s.phase=MP_PLAY;
            }
            assert(s.phase==MP_PLAY && s.device[a]!=s.device[b]);
            assert(!memcmp(s.chosen,chosen,players*sizeof(int)));
            /* Either winner keeps their accepted duel pad through loot. */
            for(int winner=0;winner<2;winner++) {
                MpSession loot=s;int p=winner?b:a,device=loot.device[p];
                loot.required=1u<<p;mp_use_choices(&loot,p,1);
                assert(loot.phase==MP_PLAY && loot.device[p]==device);
                MpSession ai=loot;ai.required=0;mp_use_choices(&ai,-1,0);
                for(int q=0;q<players;q++)assert(ai.device[q]==MP_NONE || ai.device[q]==chosen[q]);
                ai.required=1u<<p;mp_use_choices(&ai,p,1);
                assert(ai.phase==MP_PLAY && ai.device[p]==chosen[p]);
                mp_use_choices(&loot,p,0);assert(loot.phase==MP_PLAY && loot.device[p]==chosen[p]);
            }
            s.required=1u<<a;mp_use_choices(&s,a,0);
            for(int p=0;p<players;p++)assert(s.device[p]==MP_NONE || s.device[p]==s.chosen[p]);
            s.required=1u<<b;mp_use_choices(&s,b,0);duels++;
        }
        sessions++;
    }
    /* A shared group's removed instance stays invalid until one fresh claim;
     * reconnecting the group cannot reassign the keyboard or another pad. */
    for(int active=0;active<4;active++) {
        MpSession s;mp_begin_campaign(&s,4,1,1);
        for(int p=0;p<4;p++)choose(&s,p,p==3?MP_KEYBOARD:100);
        s.required=1u<<active;mp_use_choices(&s,active,0);mp_removed(&s,100);
        assert(s.disconnected==7 && s.chosen[3]==MP_KEYBOARD);
        s.required=4;mp_use_choices(&s,2,0);
        assert(s.phase==MP_CLAIM && s.claim==2 && !mp_claim(&s,MP_KEYBOARD));
        assert(mp_claim(&s,101));s.phase=MP_PLAY;
        assert(!s.disconnected && s.chosen[0]==101 && s.chosen[1]==101 && s.chosen[2]==101);
        for(int p=0;p<4;p++) {
            s.required=1u<<p;mp_use_choices(&s,p,0);
            assert(s.phase==MP_PLAY && s.device[p]==(p==3?MP_KEYBOARD:101));
        }
        /* Loss of a temporary duel pad must not replace the group's choice. */
        s.required=1;mp_use_choices(&s,0,0);s.required=3;mp_use_choices(&s,0,1);
        assert(mp_claim(&s,200));s.phase=MP_PLAY;mp_removed(&s,200);
        assert(s.phase==MP_CLAIM && s.claim==1 && !s.disconnected);
        assert(mp_claim(&s,201) && s.chosen[1]==101);
    }
    printf("PASS: %u device-choice sessions, %u automatic turns, %u shared/personal duels, %u temporary keyboard duels and shared reconnect/temporary-pad loss\n",sessions,turns,duels,keyboards);
}
static void reconnect_choices(void) {
    unsigned cases=0;
    /* Every assignment of two pads across 2..4 players, including personal and
     * shared groups. Recovery must preserve the setup groups even when another
     * group's pad is idle or temporarily has no active device[] owner. */
    for(int players=2;players<=4;players++)for(unsigned layout=1;layout<(1u<<players)-1;layout++)
    for(int active=0;active<players;active++)for(int remembered_only=0;remembered_only<2;remembered_only++) {
        MpSession s;int expected[4];mp_begin_campaign(&s,players,1,1);
        for(int p=0;p<players;p++) {
            expected[p]=100+!!(layout&(1u<<p));choose(&s,p,expected[p]);
        }
        for(int round=0;round<3;round++) {
            s.required=1u<<active;mp_use_choices(&s,active,0);
            int missing=expected[active],other=MP_NONE;
            unsigned group=0;
            for(int p=0;p<players;p++) {
                if(expected[p]==missing)group|=1u<<p;
                else other=expected[p];
            }
            mp_removed(&s,missing);
            assert(s.phase==MP_CLAIM && s.claim==active && s.disconnected==group);
            if(remembered_only)for(int p=0;p<players;p++)
                if(expected[p]==other)s.device[p]=MP_NONE;
            MpSession before=s;
            assert(!mp_can_claim(&s,other));
            assert(!mp_claim(&s,other) && !memcmp(&s,&before,sizeof(s)));
            assert(!mp_claim(&s,MP_KEYBOARD) && !memcmp(&s,&before,sizeof(s)));
            int replacement=200+round;
            assert(mp_claim(&s,replacement));s.phase=MP_PLAY;
            for(int p=0;p<players;p++)if(expected[p]==missing)expected[p]=replacement;
            assert(!s.disconnected && !memcmp(s.chosen,expected,players*sizeof(int)));
            for(int p=0;p<players;p++) {
                s.required=1u<<p;mp_use_choices(&s,p,0);
                assert(s.phase==MP_PLAY && s.device[p]==expected[p]);
            }
        }
        cases++;
    }
    /* The same borrowed controller is still allowed for a duel when the usual
     * pad is connected. Losing the loan must not turn it into a permanent choice. */
    MpSession s;mp_begin_campaign(&s,3,1,1);
    choose(&s,0,100);choose(&s,1,100);choose(&s,2,101);
    s.required=1;mp_use_choices(&s,0,0);s.required=3;mp_use_choices(&s,0,1);
    assert(mp_claim(&s,101) && s.chosen[1]==100 && s.chosen[2]==101);
    s.phase=MP_PLAY;mp_removed(&s,101);
    assert(s.claim==1 && s.disconnected==4);
    assert(mp_claim(&s,201) && s.chosen[1]==100 && s.chosen[2]==101);
    s.phase=MP_PLAY;s.required=2;mp_use_choices(&s,1,1);
    assert(s.device[1]==201);
    s.required=1;mp_use_choices(&s,0,0);s.required=4;mp_use_choices(&s,2,0);
    assert(s.phase==MP_CLAIM && s.claim==2 && !mp_claim(&s,100));
    assert(mp_claim(&s,202) && s.chosen[0]==100 && s.chosen[1]==100 && s.chosen[2]==202);
    printf("PASS: %u reconnect layouts with three repeated losses each preserve player groups; reserved idle pads rejected; temporary duel loans remain temporary\n",cases);
}
int main(int argc,char **argv) {
    const char *log=NULL,*fixture=NULL,*retail=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--log") && i+1<argc)log=argv[++i];
        else if(!strcmp(argv[i],"--fixture") && i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--dataset") && i+1<argc)g_dataset=argv[++i];
        else if(!strcmp(argv[i],"--retail-image") && i+1<argc)retail=argv[++i];
    }
    if(!log || !fixture || !retail)return 2;
    g_log=fopen(log,"w");assert(g_log);m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    assert(load_state(fixture));g_lineage=LIN_CRACKED;combat_oracle();choices();reconnect_choices();
    FILE *f=fopen(retail,"rb");unsigned char header[20];assert(f && fread(header,1,20,f)==20);
    unsigned regs=header[16]|header[17]<<8|header[18]<<16|header[19]<<24;
    assert(regs<=SAVE_NREGS && !fseek(f,20+regs*4,SEEK_SET));
    assert(fread(g_ram,1,RAM_SIZE,f)==RAM_SIZE);assert(!fclose(f));
    g_lineage=LIN_RETAIL;combat_oracle();fclose(g_log);
    return 0;
}
