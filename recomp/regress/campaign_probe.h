/* Included only in the virtual-controller probe. Drive the real menu, character
 * selection and campaign; mutations below are explicitly encounter fixtures. */
static VirtualPad campaign_pads[4];
static int campaign_players=4,campaign_pad_count=2,campaign_step,campaign_age;
static int campaign_pick=-1,campaign_pick_step,campaign_pick_age,campaign_turns;
static int campaign_last_owner=-1,campaign_claims[4],campaign_duel;
static int campaign_saved_turn;
static const char *campaign_save;
static int campaign_reverse,campaign_duel_request,campaign_duel_started,campaign_duel_polls;
static int campaign_winner_device,campaign_loser_device,campaign_loot_x,campaign_load_checked;
static int campaign_wait_fire;
static int campaign_expected_context,campaign_recover_index,campaign_recover_player,campaign_prior_devices[4];
static unsigned campaign_polled_word[4];
static unsigned campaign_prompt_done;
static int campaign_prompt_age,campaign_prompt_devices[4],campaign_prompt_white;
static int campaign_keyboard_player=-1;
static unsigned campaign_names_checked;
static char campaign_prompt_expected[64];
static int campaign_prompt_device;
static int campaign_prompt_was_claim;
static int campaign_duel_after_setup;
static int campaign_scene_checks;
static int campaign_zone_owner=-1,campaign_zone_node=20,campaign_zone_request,campaign_zone_started;
static int campaign_zone_age,campaign_zone_before;
static unsigned campaign_zone_reads,campaign_zone_word;
static int campaign_duel_opponent=-1;
static int campaign_duel_keyboard,campaign_duel_keyboard_before,campaign_keyboard_prompt_age;
static int campaign_opponent(void) { return campaign_duel_opponent>=0?campaign_duel_opponent:campaign_players-1; }

static int campaign_choice(void) {
    if(campaign_duel_keyboard && !(g_mp.enrolled&(1u<<g_mp.claim))) {
        /* Reported setup: P1/P2 share pad0, P3 uses pad1; optional P4 shares. */
        int which=(g_mp.claim?g_mp.claim-1:0)%campaign_pad_count;
        CHECK(mp_can_claim(&g_mp,campaign_pads[which].id));return campaign_pads[which].id;
    }
    if(g_mp.claim==campaign_keyboard_player && !(g_mp.enrolled&(1u<<g_mp.claim))) {
        CHECK(mp_keyboard_claimable());return MP_KEYBOARD;
    }
    for(int pass=0;pass<2;pass++) for(int i=0;i<campaign_pad_count;i++) {
        int which=(i+g_mp.claim+1)%campaign_pad_count,id=campaign_pads[which].id;
        if(mp_can_claim(&g_mp,id) && (pass || mp_owner(&g_mp,id)<0)) return id;
    }
    return mp_keyboard_claimable()?MP_KEYBOARD:MP_NONE;
}
static void campaign_press_device(int device) {
    if(device==MP_KEYBOARD) key_event(SDL_SCANCODE_RETURN,SDLK_RETURN,1);
    else for(int i=0;i<campaign_pad_count;i++)
        if(campaign_pads[i].id==device) button(campaign_pads[i],SDL_CONTROLLER_BUTTON_START,1);
}

static void campaign_guest(unsigned pc) {
    if(probe_live>=7 && pc==0x22fd0) campaign_wait_fire=1;
    if(probe_live>=7 && pc==0x22fe4) campaign_wait_fire=0;
    if(campaign_zone_request && pc==0x40188 && r16(pc)==0x2079) {
        campaign_zone_request=0;campaign_zone_started=1;
        uint32_t actor=0x2e7dc+campaign_zone_owner*0x84;
        uint32_t node=r32(0x2dfda)+20*campaign_zone_node,sp=m68k_get_reg(NULL,M68K_REG_SP)-4;
        /* Isolate the reported stale-slot condition, then run the original lair
         * handler, disk loading, monsters and combat loop without substitutions. */
        w32(0x2ebd0,actor);w16(0x2f9da,(uint16_t)campaign_zone_owner);
        w32(0x2e0c0,0x2e7dc+((campaign_zone_owner+3)%4)*0x84);
        w16(0x2f9f0,0);w32(sp,pc);m68k_set_reg(M68K_REG_SP,sp);
        m68k_set_reg(M68K_REG_A0,actor);m68k_set_reg(M68K_REG_A1,node);
        m68k_set_reg(M68K_REG_PC,0x21ca4);
        fprintf(g_log,"PROBE-ZONE launch owner=%d node=%u stale=%x\n",campaign_zone_owner,campaign_zone_node,r32(0x2e0c0));fflush(g_log);
    }
    if(campaign_zone_started && pc==0x22fc4) {
        int p=mp_roster_player(m68k_get_reg(NULL,M68K_REG_A0),1);
        if(p>=0) {
            campaign_zone_reads|=1u<<p;
            campaign_zone_word=m68k_get_reg(NULL,r8(0x2e7dc+p*0x84+0xb)==1?M68K_REG_D0:M68K_REG_D1)&0xffff;
        }
    }
    if(probe_live!=8) return;
    if(campaign_duel_request && pc==0x40188 && r16(pc)==0x2079) {
        campaign_duel_request=0;campaign_duel_started=1;
        uint32_t a=0x2e7dc+(campaign_reverse?campaign_opponent():0)*0x84;
        uint32_t b=0x2e7dc+(campaign_reverse?0:campaign_opponent())*0x84;
        uint32_t sp=m68k_get_reg(NULL,M68K_REG_SP)-4;
        /* Arrange one actor encounter. The original 21ab4 function assigns
         * ports, loads the arena, fights, chooses the winner and opens loot. */
        w8(a+0x12,0);w8(b+0x12,0); /* no protection-scroll detour in this fixture */
        w32(0x2ebd0,a);w16(0x2f9da,(uint16_t)(campaign_reverse?campaign_opponent():0));
        /* The arranged attacker must also own their map turn. In the reverse
         * shared-pad case this makes the eventual P1 winner borrow a spare. */
        mp_campaign_context(MP_CAM_MAP,mp_roster_player(a,1),0);
        w32(sp,pc);m68k_set_reg(M68K_REG_SP,sp);
        m68k_set_reg(M68K_REG_A0,a);m68k_set_reg(M68K_REG_A1,b);
        m68k_set_reg(M68K_REG_PC,0x21ab4);
        fprintf(g_log,"PROBE-DUEL launch a=%x b=%x colours=%u,%u\n",a,b,r32(a+0x36),r32(b+0x36));fflush(g_log);
    }
    if(pc==0x22fc4 && campaign_duel_started) {
        int p=mp_roster_player(m68k_get_reg(NULL,M68K_REG_A0),1);
        if(p>=0) {
            campaign_duel_polls|=1<<p;
            campaign_polled_word[p]=m68k_get_reg(NULL,r8(0x2e7dc+p*0x84+0xb)==1?M68K_REG_D0:M68K_REG_D1)&0xffff;
        }
    }
    if(pc==0x21ad0 || pc==0x2173c) {
        fprintf(g_log,"PROBE-DUEL pc=%x pair=%x,%x polls=%x started=%d\n",pc,r32(0x2e0bc),r32(0x2e0c0),campaign_duel_polls,campaign_duel_started);fflush(g_log);
    }
}

static void campaign_button(int player, SDL_GameControllerButton key, int down) {
    int id=g_mp.device[player];
    if (id==MP_KEYBOARD) {
        SDL_Scancode sc=key==SDL_CONTROLLER_BUTTON_A?SDL_SCANCODE_LCTRL:
            key==SDL_CONTROLLER_BUTTON_START?SDL_SCANCODE_RETURN:
            key==SDL_CONTROLLER_BUTTON_BACK?SDL_SCANCODE_E:
            key==SDL_CONTROLLER_BUTTON_DPAD_UP?SDL_SCANCODE_UP:
            key==SDL_CONTROLLER_BUTTON_DPAD_DOWN?SDL_SCANCODE_DOWN:
            key==SDL_CONTROLLER_BUTTON_DPAD_LEFT?SDL_SCANCODE_LEFT:
            key==SDL_CONTROLLER_BUTTON_DPAD_RIGHT?SDL_SCANCODE_RIGHT:SDL_SCANCODE_UNKNOWN;
        if (sc) key_event(sc,SDL_GetKeyFromScancode(sc),down);
    } else for(int i=0;i<campaign_pad_count;i++)
        if (campaign_pads[i].id==id) button(campaign_pads[i],key,down);
}
static void campaign_release(void) {
    for(int i=0;i<campaign_pad_count;i++) if(campaign_pads[i].joy)
        for(int b=0;b<SDL_CONTROLLER_BUTTON_MAX;b++) button(campaign_pads[i],(SDL_GameControllerButton)b,0);
    for(int sc=0;sc<SDL_NUM_SCANCODES;sc++) if(probe_keys[sc])
        key_event((SDL_Scancode)sc,SDL_GetKeyFromScancode((SDL_Scancode)sc),0);
}
/* Read the presented pixels as well as the state: an invisible or one-frame
 * handoff must fail even if its controller still works. */
static int campaign_text_pixels(SDL_Renderer *ren) {
    int width,height,white=0;
    CHECK(SDL_GetRendererOutputSize(ren,&width,&height)==0);
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface && SDL_RenderReadPixels(ren,NULL,surface->format->format,surface->pixels,surface->pitch)==0);
    for(int y=124*height/256;y<132*height/256;y++) {
        Uint32 *row=(Uint32 *)((Uint8 *)surface->pixels+y*surface->pitch);
        for(int x=40*width/320;x<280*width/320;x++)
            if((row[x]&0xffffff)==0xeef0f4) white++;
    }
    SDL_FreeSurface(surface);
    return white;
}
static int campaign_prompt_pixels(SDL_Renderer *ren) {
    int white=campaign_text_pixels(ren);
    CHECK(white>100);
    return white;
}
/* Observe real guest boundaries before this driver presses the next controls.
 * A new scene may need identification, but never an all-controls release gate. */
static void campaign_scene_check(SDL_Renderer *ren) {
    static int context=-1,owner=-2;
    static unsigned required=~0u;
    if(!g_mp_campaign) return;
    if(context==g_mp_context && owner==g_mp_ui_player && required==g_mp.required) return;
    context=g_mp_context;owner=g_mp_ui_player;required=g_mp.required;
    CHECK(g_mp.phase!=MP_RELEASE);
    if(g_mp.phase!=MP_PLAY) return;
    char text[64];
    CHECK(!mp_prompt(1,text) && !campaign_text_pixels(ren));
    fprintf(g_log,"PROBE-SCENE-PLAY game=%d context=%d owner=%d required=%x devices=%d,%d,%d,%d\n",
            g_cur_frame,g_mp_context,g_mp_ui_player,g_mp.required,
            g_mp.device[0],g_mp.device[1],g_mp.device[2],g_mp.device[3]);
    if(campaign_scene_checks<12) {
        char suffix[64];snprintf(suffix,sizeof(suffix),"scene-play-%02d",campaign_scene_checks);
        capture_named_overlay(ren,suffix);
    }
    campaign_scene_checks++;
}
static int campaign_selection_prompt(SDL_Renderer *ren) {
    int p=g_mp_ui_player;
    if(probe_live!=7 || !g_mp_campaign || g_mp.shared || g_mp_context!=MP_CAM_SELECT
        || p<1 || (campaign_prompt_done&(1u<<p))) return 0;
    /* A shared setup can become personal on the last player's first choice;
     * that Enter/Start already acknowledges their selection handoff. */
    if(campaign_pad_count<campaign_players && campaign_players>2
        && !campaign_prompt_age && g_mp.phase!=MP_CONFIRM) return 0;
    int age=++campaign_prompt_age;
    char text[64];
    const char *prompt=mp_prompt(1,text);
    if(age==1) {
        capture_named_overlay(ren,p==1?"prompt-p2":p==2?"prompt-p3":"prompt-p4");
        const char *action=g_mp.phase==MP_CLAIM
            ? (mp_keyboard_claimable()?"START OR ENTER":"START")
            : (g_mp.device[p]==MP_KEYBOARD?"ENTER":"START");
        campaign_prompt_device=g_mp.phase==MP_CLAIM?campaign_choice():g_mp.device[p];
        campaign_prompt_was_claim=g_mp.phase==MP_CLAIM;
        if(g_mp.phase==MP_CLAIM && campaign_prompt_device==MP_KEYBOARD && campaign_pad_count==1)
            action="ENTER";
        snprintf(campaign_prompt_expected,sizeof(campaign_prompt_expected),"PLAYER %d PRESS %s",p+1,action);
        CHECK(prompt && !strcmp(prompt,campaign_prompt_expected));
        CHECK(campaign_prompt_device!=MP_NONE);
        memcpy(campaign_prompt_devices,g_mp.device,sizeof(campaign_prompt_devices));
        probe_icount=g_icount;probe_guest_frame=g_cur_frame;
        campaign_prompt_white=campaign_prompt_pixels(ren);
    }
    if(age>=2 && age<=65) check_frozen();
    for(int q=0;q<campaign_players;q++)
        CHECK(g_mp.device[q]==(q==p && age>=62?campaign_prompt_device:campaign_prompt_devices[q]));
    if(age<=61 && !(age>=41 && age<=45)) {
        CHECK(prompt && !strcmp(prompt,campaign_prompt_expected));
        if(!(age%10)) CHECK(campaign_prompt_pixels(ren)==campaign_prompt_white);
    }
    /* Wrong player / wrong device / gameplay buttons cannot dismiss it. */
    if(age==10) campaign_button(0,SDL_CONTROLLER_BUTTON_START,1);
    if(age==20 && !mp_keyboard_claimable() && campaign_prompt_device!=MP_KEYBOARD)
        key_event(SDL_SCANCODE_RETURN,SDLK_RETURN,1);
    if(age==30) key_event(SDL_SCANCODE_LCTRL,SDLK_LCTRL,1);
    if(age==40) focus_event(ren,0);
    if(age>=41 && age<=45) CHECK(!strcmp(mp_prompt(0,text),"PAUSED"));
    if(age==45) focus_event(ren,1);
    if(age==60) capture_named_overlay(ren,"prompt-waited");
    if(age>=61 && age<65) campaign_press_device(campaign_prompt_device);
    if(age>=62 && age<=65) CHECK(!prompt && !campaign_text_pixels(ren));
    if(age==66) {
        CHECK(g_mp.phase==MP_PLAY && !prompt && g_icount>probe_icount);
        campaign_prompt_done|=1u<<p;campaign_prompt_age=0;
        if(campaign_prompt_was_claim) campaign_claims[p]++;
        printf("PASS: P%d selection prompt remains visible and frozen, ignores other devices, survives focus loss and waits for Start/Enter release\n",p+1);
        fflush(stdout);
    }
    return 1;
}
static void campaign_duel_present(SDL_Renderer *ren) {
    int p=0,q=campaign_opponent();
    unsigned required=(1u<<p)|(1u<<q);
    uint32_t winner=0x2e7dc+p*0x84,loser=0x2e7dc+q*0x84;
    if(campaign_step==0) {
        CHECK(g_mp_campaign && g_mp.players==campaign_players);
        campaign_duel_request=1;campaign_step=1;campaign_age=0;
    } else if(campaign_step==1 && g_mp_context==MP_CAM_COMBAT && campaign_duel_polls==(int)required) {
        CHECK(g_mp.required==required);
        CHECK(g_mp.device[p]!=g_mp.device[q] && g_mp.device[p]!=MP_NONE && g_mp.device[q]!=MP_NONE);
        if(campaign_duel_keyboard) {
            CHECK(campaign_keyboard_prompt_age==10);
            CHECK(g_mp.device[campaign_reverse?p:q]==MP_KEYBOARD);
            CHECK(g_mp.chosen[p]==campaign_pads[0].id && g_mp.chosen[q]==campaign_pads[0].id);
        }
        CHECK(r8(winner+0xb)==(campaign_reverse?1:2) && r8(loser+0xb)==(campaign_reverse?2:1));
        campaign_winner_device=g_mp.device[p];campaign_loser_device=g_mp.device[q];
        campaign_step=2;campaign_age=0;
        capture_named_overlay(ren,"duel");
    } else if(campaign_step==2) {
        campaign_age++;
        if(campaign_age<=12) {
            campaign_button(p,campaign_reverse?SDL_CONTROLLER_BUTTON_DPAD_RIGHT:SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
            campaign_button(q,campaign_reverse?SDL_CONTROLLER_BUTTON_DPAD_LEFT:SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
        } else if(campaign_age==13) {
            fprintf(g_log,"PROBE-DUEL directions words=%u,%u raw=%u,%u inputs=%u,%u xy=%u,%u reverse=%d\n",r16(winner+0x3e),r16(loser+0x3e),
                campaign_polled_word[p],campaign_polled_word[q],g_mp_input[p],g_mp_input[q],r16(winner+4),r16(loser+4),campaign_reverse);fflush(g_log);
            unsigned a=campaign_reverse?1:2,b=campaign_reverse?2:1;
            CHECK(campaign_polled_word[p]==a && campaign_polled_word[q]==b);
            CHECK(r16(winner+0x3e)==a && r16(loser+0x3e)==b);
            CHECK(campaign_save && save_state(campaign_save));
            CHECK(load_state(campaign_save));
            CHECK(g_mp_campaign && g_mp_context==MP_CAM_COMBAT && g_mp.required==required);
            CHECK(g_mp.device[p]==campaign_winner_device && g_mp.device[q]==campaign_loser_device);
            campaign_load_checked=1;
        } else if(campaign_age==17) {
            /* Arrange lethal range; damage, animations, winner selection and
             * loot transition must still come from original attacks. */
            w16(winner+4,150);w16(loser+4,130);
            w16(winner+8,100);w16(loser+8,100);w16(loser+0x50,1);
        } else if(campaign_age>18 && g_mp_context==MP_CAM_COMBAT) {
            if(campaign_age%50<25) {
                campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
                campaign_button(p,SDL_CONTROLLER_BUTTON_A,1);
            }
        }
        if(g_mp_context==MP_CAM_UI) {
            CHECK(g_mp_ui_player==p && g_mp.device[p]==campaign_winner_device);
            CHECK(r32(0x2e0bc)==winner && campaign_load_checked);
            campaign_step=3;campaign_age=0;
        }
    } else if(campaign_step==3) {
        campaign_age++;
        if(campaign_age==40) {
            campaign_loot_x=r16(0x392d4);capture_named_overlay(ren,"loot");
            char path[1200];snprintf(path,sizeof(path),"%s.loot.sav",campaign_save);
            CHECK(save_state(path));
            MpSession before=g_mp;
            CHECK(load_state(path) && g_mp_context==MP_CAM_UI && !memcmp(&before,&g_mp,sizeof(g_mp)));
        }
        if(campaign_age>=40 && campaign_age<50) campaign_button(q,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
        if(campaign_age==50) CHECK(r16(0x392d4)==campaign_loot_x);
        if(campaign_age>=50 && campaign_age<60) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
        if(campaign_age==60) {
            CHECK(r16(0x392d4)<campaign_loot_x);
            printf("PASS: P1 vs P%d campaign duel (%s); distinct input roles, simultaneous movement, warm reload, original lethal attack and winner-only loot\n",q+1,campaign_reverse?"P1 defends":"P1 initiates");
            if(campaign_duel_keyboard) { campaign_step=5;campaign_age=0; }
            else { SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4; }
        }
    } else if(campaign_step==5) {
        CHECK(++campaign_age<600 && g_mp.phase==MP_PLAY);
        if(g_mp_context==MP_CAM_MAP && g_map_live && g_cur_frame-g_map_live<3
            && g_mp_ui_player==mp_roster_player(r32(0x2ebd0),1)) {
            CHECK(mp_owner(&g_mp,MP_KEYBOARD)<0);
            CHECK(g_mp.chosen[0]==campaign_pads[0].id && g_mp.chosen[q]==campaign_pads[0].id);
            for(int i=0;i<campaign_players;i++) CHECK(g_mp.device[i]==MP_NONE || g_mp.device[i]==g_mp.chosen[i]);
            CHECK(g_mp.device[g_mp_ui_player]==g_mp.chosen[g_mp_ui_player]);
            char path[1200];snprintf(path,sizeof(path),"%s.map.sav",campaign_save);
            CHECK(save_state(path));MpSession before=g_mp;
            CHECK(load_state(path) && !memcmp(before.chosen,g_mp.chosen,sizeof(g_mp.chosen)));
            capture_named_overlay(ren,"returned-map");
            /* A map save initially reconstructs the last UI participant; the
             * next original map boundary reselects its actual turn owner. */
            campaign_step=6;campaign_age=0;
        } else if(g_mp_context==MP_CAM_UI) {
            int x=r16(0x392d4),y=r16(0x392d6);
            /* Duel loot places Exit at the screen edge, unlike initial equip.
             * Use the original live hotspot rather than assume either layout. */
            unsigned hotspot=r32(0x3a96c);
            CHECK(hotspot<RAM_SIZE-24 && r32(hotspot+0x10)==7);
            int tx=r16(hotspot+0xc)+r16(hotspot+4)/2,ty=r16(hotspot+0xe)+r16(hotspot+6)/2;
            if(campaign_wait_fire) campaign_button(p,SDL_CONTROLLER_BUTTON_A,campaign_age%20<10);
            else if(x>tx+2) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
            else if(x<tx-2) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
            else if(y<ty-2) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
            else if(y>ty+2) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_UP,1);
            else if(!(campaign_age%10)) campaign_button(p,SDL_CONTROLLER_BUTTON_A,1);
        }
    } else if(campaign_step==6) {
        CHECK(++campaign_age<400 && g_mp.phase==MP_PLAY && mp_owner(&g_mp,MP_KEYBOARD)<0);
        if(g_mp_context==MP_CAM_MAP && g_map_live && g_cur_frame-g_map_live<3
            && g_mp_ui_player==mp_roster_player(r32(0x2ebd0),1)) {
            CHECK(g_mp.device[g_mp_ui_player]==g_mp.chosen[g_mp_ui_player]);
            puts("PASS: explicit duel keyboard, pending/combat/loot/map warm saves, original loot exit and restored shared controller on map");
            SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4;
        }
    }
}
static void campaign_restore_present(SDL_Renderer *ren) {
    if(campaign_step==0) {
        CHECK(g_mp_campaign && g_mp_context==campaign_expected_context && g_mp.phase==MP_PLAY);
        CHECK(campaign_save && save_state(campaign_save));
        memcpy(campaign_prior_devices,g_mp.device,sizeof(campaign_prior_devices));
        campaign_recover_player=g_mp_ui_player;
        /* The original attacking/UI slot may now be the keyboard player.
         * Recover a controller that this scene actually requires. */
        if(g_mp.device[campaign_recover_player]==MP_KEYBOARD)
            for(int p=0;p<campaign_players;p++) if((g_mp.required&(1u<<p)) && g_mp.device[p]>=0) {
                campaign_recover_player=p;break;
            }
        campaign_recover_index=-1;
        for(int i=0;i<campaign_pad_count;i++) if(campaign_pads[i].id==g_mp.device[campaign_recover_player]) campaign_recover_index=i;
        CHECK(campaign_recover_index>=0);
        probe_icount=g_icount;probe_guest_frame=g_cur_frame;
        focus_event(ren,0);campaign_step=1;campaign_age=0;
    } else if(campaign_step==1) {
        int age=++campaign_age;
        if(age<=8) check_frozen();
        if(age==2) detach_pad(&campaign_pads[campaign_recover_index]);
        if(age==3) {
            CHECK(g_mp.phase==MP_CLAIM && g_mp.claim==campaign_recover_player && g_mp.device[campaign_recover_player]==MP_NONE);
            for(int p=0;p<campaign_players;p++) if(p!=campaign_recover_player) CHECK(g_mp.device[p]==campaign_prior_devices[p]);
            campaign_pads[campaign_recover_index]=attach_pad();
        }
        if(age==5) focus_event(ren,1);
        if(age==6 || age==7) button(campaign_pads[campaign_recover_index],SDL_CONTROLLER_BUTTON_START,1);
        if(age==8) {
            CHECK(g_mp.phase==MP_RELEASE && g_mp.device[campaign_recover_player]==campaign_pads[campaign_recover_index].id);
            for(int p=0;p<campaign_players;p++) if(p!=campaign_recover_player) CHECK(g_mp.device[p]==campaign_prior_devices[p]);
        }
        if(age==11) {
            CHECK(g_mp.phase==MP_PLAY && g_icount>probe_icount);
            CHECK(load_state(campaign_save));
            CHECK(g_mp_context==campaign_expected_context && g_mp.device[campaign_recover_player]==campaign_pads[campaign_recover_index].id);
            for(int p=0;p<campaign_players;p++) if(p!=campaign_recover_player) CHECK(g_mp.device[p]==campaign_prior_devices[p]);
        }
        if(age==15) {
            CHECK(g_mp.phase==MP_PLAY && g_mp_context==campaign_expected_context);
            if(probe_menu_fixture) {
                char text[64];
                CHECK(load_state(probe_menu_fixture));
                CHECK(!mp_active() && g_mp.phase==MP_OFF && !mp_prompt(1,text));
            }
            printf("PASS: campaign cold restore context %d, focus freeze, required-pad loss/replacement, surviving assignments and warm reload\n",campaign_expected_context);
            SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4;
        }
    }
}
static void campaign_present(SDL_Renderer *ren) {
    int n=++probe_frame;
    CHECK(n<4500);
    if (!(n%250)) { fprintf(g_log,"PROBE-CAM n=%d step=%d players=%u row=%u context=%d owner=%d phase=%d pickstep=%d name=%u remaining=%u cursor=%u,%u\n",
        n,campaign_step,r16(0x2e024),r16(0x3051e),g_mp_context,g_mp_ui_player,g_mp.phase,campaign_pick_step,
        r16(0x2e05c),r16(0x306ec),r16(0x392d4),r16(0x392d6));fflush(g_log); }
    campaign_release();
    if(campaign_zone_started && g_mp_context==MP_CAM_COMBAT) {
        int p=campaign_zone_owner;
        if(campaign_zone_before) {
            CHECK(g_mp.required!=1u<<p && g_mp.phase==MP_CLAIM);
            capture_named_overlay(ren,"false-second-player");
            CHECK(save_state(campaign_save));
            printf("REPRO: monster fight for P%d requests mask %x, claim P%d\n",p+1,g_mp.required,g_mp.claim+1);
            SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4;
        } else {
            CHECK(g_mp.required==1u<<p && g_mp_ui_player==p && g_mp.phase==MP_PLAY);
            if(!campaign_zone_reads) { SDL_RenderPresent(ren);return; }
            int age=++campaign_zone_age;
            if(age<12) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
            if(age==12) {
                fprintf(g_log,"PROBE-ZONE reads=%x word=%u actor=%u input=%u\n",campaign_zone_reads,campaign_zone_word,r16(0x2e7dc+p*0x84+0x3e),g_mp_input[p]);fflush(g_log);
                CHECK(campaign_zone_reads==1u<<p && campaign_zone_word==2 && r16(0x2e7dc+p*0x84+0x3e)==2);
                CHECK(save_state(campaign_save));
                MpSession before=g_mp;
                CHECK(load_state(campaign_save) && !memcmp(&before,&g_mp,sizeof(g_mp)));
                capture_named_overlay(ren,"zone-playing");
            }
            if(age==18) {
                printf("PASS: P%d monster fight uses only its selected device, native movement and warm load; stale knight excluded\n",p+1);
                SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4;
            }
        }
        SDL_RenderPresent(ren);return;
    }
    if(campaign_selection_prompt(ren)) {SDL_RenderPresent(ren);return;}
    if(probe_live==9 && campaign_expected_context==0) {
        char text[64];
        CHECK(!mp_active() && g_mp.phase==MP_OFF && !mp_prompt(1,text));
        if(n==10) {
            puts("PASS: cold menu save with Players 4 / Select Knight stays outside campaign setup");
            SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4;
        }
        SDL_RenderPresent(ren);return;
    }
    if(probe_live==9 && campaign_step>0) {campaign_restore_present(ren);SDL_RenderPresent(ren);return;}
    if(campaign_duel_keyboard && campaign_duel_started && g_mp.phase==MP_CLAIM) {
        int age=++campaign_keyboard_prompt_age,p=campaign_reverse?0:campaign_opponent();
        CHECK(campaign_step==1 && g_mp.claim==p && g_mp.device[p]==MP_NONE);
        char text[64],expected[64];
        snprintf(expected,sizeof(expected),"PLAYER %d PRESS %s",p+1,campaign_duel_keyboard_before?"START":"START OR ENTER");
        CHECK(!strcmp(mp_prompt(1,text),expected));
        CHECK(mp_keyboard_claimable()==!campaign_duel_keyboard_before);
        if(age==1) {
            probe_icount=g_icount;probe_guest_frame=g_cur_frame;
            capture_named_overlay(ren,"keyboard-choice");
        } else check_frozen();
        if(age==4 && !campaign_duel_keyboard_before) {
            char path[1200];snprintf(path,sizeof(path),"%s.claim.sav",campaign_save);
            CHECK(save_state(path));MpSession before=g_mp;
            CHECK(load_state(path) && !memcmp(&before,&g_mp,sizeof(g_mp)));
        }
        if(age==10) key_event(campaign_reverse?SDL_SCANCODE_KP_ENTER:SDL_SCANCODE_RETURN,
                             campaign_reverse?SDLK_KP_ENTER:SDLK_RETURN,1);
        if(campaign_duel_keyboard_before && age==12) {
            CHECK(mp_owner(&g_mp,MP_KEYBOARD)<0);
            puts("REPRO: shared-controller duel rejects Enter with an unused keyboard and offers only Start");
            SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);campaign_step=4;
        }
        SDL_RenderPresent(ren);return;
    }
    if (g_mp.phase==MP_CLAIM && g_mp_campaign) {
        if(campaign_players==2 && campaign_pad_count<=2 && !g_mp.recovery && g_mp.claim==0) {
            char text[64];CHECK(!strcmp(mp_prompt(1,text),"PLAYER 1 PRESS START OR ENTER"));
        }
        if (!(n%4)) {
            campaign_press_device(campaign_choice());
            campaign_claims[g_mp.claim]++;
        }
        SDL_RenderPresent(ren); return;
    }
    if(g_mp_campaign && g_mp.phase==MP_CONFIRM) {
        if(!(n%4)) campaign_button(g_mp.claim,SDL_CONTROLLER_BUTTON_START,1);
        SDL_RenderPresent(ren);return;
    }
    if (g_mp_campaign && g_mp.phase!=MP_PLAY) { SDL_RenderPresent(ren); return; }
    if(probe_live==8) {campaign_duel_present(ren);SDL_RenderPresent(ren);return;}
    if(probe_live==9) {campaign_restore_present(ren);SDL_RenderPresent(ren);return;}
    if(campaign_step==0) {
        if(r16(0x2e024)<campaign_players) button(campaign_pads[0],SDL_CONTROLLER_BUTTON_DPAD_RIGHT,n%12<6);
        else if(r16(0x3051e)<3) button(campaign_pads[0],SDL_CONTROLLER_BUTTON_DPAD_DOWN,n%12<6);
        else { campaign_step=1;campaign_age=0; }
    } else if(campaign_step==1) {
        if(++campaign_age%30>=15) button(campaign_pads[0],SDL_CONTROLLER_BUTTON_A,1);
        if(g_mp_campaign) { CHECK(g_mp.players==campaign_players);campaign_step=2; }
    } else if(campaign_step==2 && g_mp_context==MP_CAM_SELECT) {
        int p=g_mp_ui_player;
        CHECK(p>=0 && p<campaign_players);
        if(campaign_pick!=p) {
            campaign_pick=p; campaign_pick_step=0; campaign_pick_age=0;
            capture_named_overlay(ren,p==0?"select-p1":p==1?"select-p2":p==2?"select-p3":"select-p4");
            if(p==1 && campaign_save) {
                char path[1200];snprintf(path,sizeof(path),"%s.selection.sav",campaign_save);
                CHECK(save_state(path));
            }
        }
        campaign_pick_age++;
        if(campaign_pick_step==0) {
            int colour=3-p; /* prove logical identity is independent of colour */
            if(r16(0x3076a)!=colour) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,n%12<6);
            else { campaign_pick_step=1;campaign_pick_age=0; }
        } else if(campaign_pick_step==1) {
            if(campaign_pick_age%30>=15) campaign_button(p,SDL_CONTROLLER_BUTTON_A,1);
            if(r16(0x2e05c)==1) {campaign_pick_step=2;campaign_pick_age=0;}
        } else if(campaign_pick_step==2) {
            if(campaign_pick_age==8) key_event(SDL_SCANCODE_Z,SDLK_z,1);
            if(campaign_pick_age==15) {
                unsigned length=r16(0x2e05a),name=r32(0x30300);
                CHECK(length && length<=13 && r8(name+length-1)=='Z');
                campaign_names_checked|=1u<<p;
                capture_named_overlay(ren,p==0?"name-p1":p==1?"name-p2":p==2?"name-p3":"name-p4");
            }
            if(campaign_pick_age>=20 && campaign_pick_age%30>=15) campaign_button(p,SDL_CONTROLLER_BUTTON_A,1);
            if(campaign_pick_age>35 && !r16(0x2e05c)) campaign_pick_step=3;
        }
    } else if(campaign_step==2 && (g_mp_context==MP_CAM_MAP || g_mp_context==MP_CAM_UI)) {
        CHECK(campaign_names_checked==(1u<<campaign_players)-1);
        if(campaign_keyboard_player>=0) CHECK(g_mp.device[campaign_keyboard_player]==MP_KEYBOARD);
        for(int p=0;p<campaign_players;p++) {
            CHECK(r32(0x2e7dc+p*0x84+0x36)==(unsigned)(3-p));
            CHECK(r8(0x2e7dc+p*0x84+0xb)==2);
        }
        printf("PASS: %d campaign players select original knights/names with %d controllers; colours are reversed\n",campaign_players,campaign_pad_count);
        fflush(stdout);
        campaign_step=3;campaign_age=0;
    } else if(campaign_step==3) {
        int p=g_mp_ui_player;
        if(g_mp_context==MP_CAM_MAP && p>=0) {
            if(p!=campaign_last_owner) {
                campaign_last_owner=p;campaign_age=0;campaign_turns++;campaign_saved_turn=0;
                capture_named_overlay(ren,p==0?"map-p1":p==1?"map-p2":p==2?"map-p3":"map-p4");
            }
            campaign_age++;
            if(campaign_age>=12 && !campaign_saved_turn && g_map_live && g_cur_frame-g_map_live<3) {
                if(campaign_save && !campaign_duel) CHECK(save_state(campaign_save));
                campaign_saved_turn=1;
                campaign_button(p,SDL_CONTROLLER_BUTTON_BACK,1);
            }
            if(campaign_age>12 && !(campaign_age%40)) campaign_button(p,SDL_CONTROLLER_BUTTON_BACK,1);
            if(g_cur_frame-g_map_live>8 && campaign_age%40>=20)
                campaign_button(p,SDL_CONTROLLER_BUTTON_A,1); /* original daybreak acknowledgement */
            if(campaign_turns>campaign_players) {
                int keyboard_owner=mp_owner(&g_mp,MP_KEYBOARD);
                CHECK(g_mp.shared==(campaign_players>campaign_pad_count+(keyboard_owner>=0)));
                if(campaign_keyboard_player>=0) CHECK(keyboard_owner==campaign_keyboard_player);
                if(!campaign_zone_before) for(int q=0;q<campaign_players;q++) CHECK(campaign_claims[q]==1);
                printf("PASS: original campaign turn rotation and assigned End Turn work for %d players / %d controllers, claims=%d,%d,%d,%d\n",
                       campaign_players,campaign_pad_count,campaign_claims[0],campaign_claims[1],campaign_claims[2],campaign_claims[3]);
                if(campaign_zone_owner>=0) {
                    campaign_zone_request=1;campaign_step=5;
                } else if(campaign_duel_after_setup) {
                    probe_live=8;campaign_step=0;campaign_age=0;
                } else {
                    SDL_Event event={.type=SDL_QUIT};SDL_PushEvent(&event);campaign_step=4;
                }
            }
        } else if(g_mp_context==MP_CAM_UI && p>=0) {
            if(campaign_wait_fire) {
                campaign_button(p,SDL_CONTROLLER_BUTTON_A,n%30>=15);
                SDL_RenderPresent(ren);return;
            }
            /* Original initial equipment screen: select EXIT at (84,100). */
            int x=r16(0x392d4);
            if(x>94) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);
            else if(x<75) campaign_button(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
            else if(!(++campaign_age%20)) campaign_button(p,SDL_CONTROLLER_BUTTON_A,1);
        }
    }
    SDL_RenderPresent(ren);
}
