/* Original 68k scroll dispatch, rotation, mode restoration and return stacks.
 * Only rendering/audio/wait leaves are substituted. Never deploy. */
#define SDL_MAIN_HANDLED
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>
enum {STACK=0x1ff000,STOP=0x1ef000,HOT=0x1d1000,LABEL=0x1d1100,TASK=0x1d1200};
typedef struct {
    unsigned entry,loop,cleanup,render,render_logic,render_tail,hit,wait,tick,blit,success,print;
    unsigned item_draw,arrow,caption,remove,draw,mode,previous,active,selected,reenter;
    unsigned actor,items,other,other_items,pair,roster,rotate,rotate_label,dragon,wyrm;
    unsigned cursor,debounce,cursor_task,key,exitflag,cursor_handler;
} Layout;
static const Layout layout[]={
    {0x2bbec,0x2bc30,0x2bc6c,0x2bca4,0x2bd36,0x2be36,0x2a502,0x29f88,0x293c4,0x29b26,0x2deb0,0x3cb3c,
     0x2c044,0x2ccaa,0x2d6e0,0x2d5f4,0x4d47a,0x2fb1c,0x2fb04,0x3f534,0x2fb02,0,
     0x2fb08,0x2fb0c,0x2fb10,0x2fb14,0x2e0bc,0x2e7dc,0x2cd04,0x39bec,0x2e9ec,0x2cfdc,
     0x392c8,0x392d8,0x392da,0x3c9a0,0x392e6,0x2d61c},
    {0x2bac4,0x2bb0a,0x2bb64,0x2bb9a,0x2bc22,0x2bd34,0x2a3be,0x29e3c,0x2929e,0x299da,0x2dc8a,0,
     0x2bf44,0x2cb76,0x2d4ba,0x2d3c2,0x4d164,0x2f8ca,0x2f8ce,0x3f218,0x2f8b2,0x2f8b4,
     0x2f8b6,0x2f8ba,0x2f8be,0x2f8c2,0x2de94,0x2e5b4,0x2cbd0,0x39942,0x2e7c4,0x2cea6,
     0x39088,0x39096,0x39098,0x3c684,0x390a4,0x2d3ea}
};
static unsigned char images[2][RAM_SIZE];
static const Layout *l;
static int edition,slot,take_item,shortcut,phase,entries,exits,rotation_left;
static unsigned target,stop_poll,mode_after_take,sp_after_take,casts,cast_goal;
static unsigned legacy_depth,preloot;
static int stuck;
static void ret(void) {
    unsigned sp=m68k_get_reg(NULL,M68K_REG_SP);
    unsigned next=r32(sp);
    m68k_set_reg(M68K_REG_PC,next);m68k_set_reg(M68K_REG_SP,sp+4);
    /* Musashi executes the redirected instruction in this same callback.
     * Preserve the production transition boundary when replacing a leaf RTS. */
    if(edition>=2)inventory_flow_hook(next);
}
static void hot(unsigned kind,unsigned field,unsigned flags,int special) {
    memset(g_ram+HOT,0,24);w32(HOT+8,special==2?l->rotate_label:LABEL);
    w32(HOT+16,special==1?7:0);w16(HOT+20,kind);w16(HOT+22,field);w16(LABEL+8,flags);
    m68k_set_reg(M68K_REG_D0,1);m68k_set_reg(M68K_REG_A0,HOT);
}
void moon_instr_hook(unsigned pc) {
    if(pc==l->entry)entries++;
    if(pc==l->cleanup)exits++;
    if(pc==l->loop || pc==l->hit) {
        if(phase==-2) {assert(r32(l->mode)==9);phase=0;}
        if(phase==0){phase=1;casts++;}
        else if(phase==1 && r32(l->mode)==(slot==14?8u:11u))phase=2;
        else if(phase==3) {
            mode_after_take=r32(l->mode);sp_after_take=m68k_get_reg(NULL,M68K_REG_SP);
            if(casts<cast_goal && slot==14 && mode_after_take==9){phase=1;casts++;}
            else phase=4;
        }
        if(phase==2 && !rotation_left && !take_item)phase=4;
        if(phase==4 && shortcut) {
            target=r32(l->other);phase=5;
            if(edition==1)w16(l->key,45);else g_inventory_close_request=1;
        } else if(phase==5 && exits) {
            /* A second poll after a single close means the outer menu survived. */
            stuck=1;stop_poll=m68k_get_reg(NULL,M68K_REG_SP);
            m68k_set_reg(M68K_REG_PC,STOP-2);return;
        }
    }
    if(pc==l->hit) {
        if(phase==-1){phase=-2;hot(5,0,32,0);}
        else if(phase==1)hot(5,slot,16,0);
        else if(phase==2 && rotation_left) {rotation_left--;hot(0,0,0,2);}
        else if(phase==2 && take_item){phase=3;target=r32(l->other);hot(5,0,slot==14?32:0,0);}
        else if(phase==4){target=r32(l->other);phase=5;hot(0,0,0,1);}
        else {m68k_set_reg(M68K_REG_D0,0);}
        ret();return;
    }
    if(pc==l->render) {
        /* Keep the native scene decisions and restoration branch, bypass only
         * the screen/font setup preceding them. Draw helpers are no-ops below. */
        m68k_set_reg(M68K_REG_PC,l->render_logic);return;
    }
    if(pc==l->render_tail){ret();return;}
    unsigned sound=edition==1?0x3b1c0:0x3b470;
    if(pc==l->blit || pc==l->wait || pc==l->tick || pc==l->success || pc==l->print
        || pc==l->item_draw || pc==l->arrow || pc==l->caption || pc==l->draw || pc==sound
        || pc==0x21474 || pc==0x214e2) {ret();return;}
    if(edition>=2) {
        production_hook(pc);
        if(pc==l->loop && m68k_get_reg(NULL,M68K_REG_PC)==l->cleanup)exits++;
    }
}
static void run_case(int version,int item,int use_item,int key,int rotate,int owner,int count) {
    edition=version;l=&layout[version==1];slot=item;take_item=use_item;shortcut=key;
    phase=entries=exits=stuck=0;rotation_left=rotate;target=stop_poll=mode_after_take=sp_after_take=casts=0;cast_goal=count;
    if(preloot)phase=-1;
    memcpy(g_ram,images[version==1],RAM_SIZE);
    g_os=version>=2;g_lineage=version==1?LIN_RETAIL:LIN_CRACKED;g_retail_parity=1;g_sdl_mode=0;
    g_inventory_flow=version!=3;
    g_stop=0;g_icount=0;g_inventory_menu_active=g_inventory_close_request=0;
    g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;m68k_set_irq(0);
    m68k_set_reg(M68K_REG_SR,0x2700);
    for(int i=0;i<15;i++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SP,STACK);w32(STACK,STOP);w16(STOP-2,0x4e71);
    if(legacy_depth) {
        w16(STOP-6,0x4eb9);w32(STOP-4,0x2bbec);
        for(unsigned n=1;n<=legacy_depth;n++)w32(STACK-4*n,0x2bc56);
        m68k_set_reg(M68K_REG_SP,STACK-4*legacy_depth);
    }
    for(unsigned p=0;p<4;p++) {
        unsigned a=l->roster+p*0x84,inv=0x1c1000+p*24;
        w32(a+0x60,inv);w32(a+0x36,p);w8(a+0x49,3);w8(a+0x46,1);
        w8(inv,p==(unsigned)owner?0:3);w8(inv+slot,2);w32(l->rotate+p*4,a);
    }
    unsigned a=l->roster+owner*0x84;
    w32(l->pair,a);w32(l->actor,a);w32(l->items,r32(a+0x60));
    w32(l->other,l->roster+((owner+1)%4)*0x84);w32(l->pair+4,r32(l->other));
    w32(l->other_items,r32(r32(l->other)+0x60));
    w32(l->mode,preloot?1:9);w32(l->previous,0x12345678);
    w16(l->active,1);w16(l->selected,0);if(l->reenter)w16(l->reenter,0);
    w16(l->cursor,1);w16(l->debounce,0);w16(l->exitflag,0);w16(l->key,0);w16(l->wyrm,0);
    unsigned task=version>=2?0x3c096:TASK;
    if(version>=2)memset(g_ram+task,0,0x24);
    w32(l->cursor_task,task);w32(task,l->cursor_handler);
    w32(l->dragon+0x64,0);
    /* The relocated flat draw target is extracted instead of guessed. */
    unsigned draw=r32(l->remove+26);assert(draw<RAM_SIZE);
    /* This leaf is stubbed directly so both original relocation layouts work. */
    w16(draw,0x4e75);
    m68k_set_reg(M68K_REG_PC,l->loop);
    unsigned steps=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=STOP) {
        if(++steps>100000){fprintf(stderr,"timeout edition=%d slot=%d phase=%d pc=%x mode=%u casts=%u entries=%d exits=%d sp=%x\n",version,item,phase,m68k_get_reg(NULL,M68K_REG_PC),r32(l->mode),casts,entries,exits,m68k_get_reg(NULL,M68K_REG_SP));abort();}
        m68k_execute(1);assert(!g_stop);
    }
    unsigned inv=r32(a+0x60);assert(r8(inv+slot)==2-casts);
    if(slot==14 && take_item)assert(r8(inv)==casts+preloot);
    else assert(r8(inv)==preloot);
    assert(!r16(l->cursor));assert(!r16(l->active));assert(!r16(l->wyrm));
    assert(r32(l->previous)==(slot==14 || version==1 || version==2 ? 9u : 0x12345678u));
    assert(stuck==(version==3 && shortcut));
    if(version==1 || version==2) {
        assert(!entries && exits==1);
        assert(m68k_get_reg(NULL,M68K_REG_SP)==STACK+4);
        if(slot==14 && take_item)assert(sp_after_take==STACK);
    }
    if(slot==16)assert(r32(l->dragon+0x64)==target);
    else assert(r32(l->dragon+0x64)==0);
    printf("ORACLE edition=%d slot=%d take=%d shortcut=%d rotate=%d owner=%d casts=%u stuck=%d entries=%d cleanups=%d stack=%x take_mode=%u take_sp=%x target=%x legacy=%u preloot=%u\n",
        version,slot,take_item,shortcut,rotate,owner+1,casts,stuck,entries,exits,stuck?stop_poll:m68k_get_reg(NULL,M68K_REG_SP),mode_after_take,sp_after_take,target,legacy_depth,preloot);
}
static void transition_flags_and_guards(void) {
    unsigned reference[16],cases=0;
    for(unsigned mode=8;mode<=11;mode+=3)for(unsigned flags=0;flags<32;flags++) {
        memcpy(g_ram,images[1],RAM_SIZE);edition=1;l=&layout[1];g_os=0;
        m68k_set_reg(M68K_REG_SR,0x2700|flags);
        for(unsigned reg=0;reg<15;reg++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+reg),0x12340000+reg);
        m68k_set_reg(M68K_REG_SP,STACK);w32(STACK,STOP);
        m68k_set_reg(M68K_REG_PC,mode==8?0x2cdce:0x2ce5c);
        for(unsigned steps=0;m68k_get_reg(NULL,M68K_REG_PC)!=STOP;steps++) {
            assert(steps<10);m68k_execute(1);
        }
        for(unsigned reg=0;reg<15;reg++)reference[reg]=m68k_get_reg(NULL,(m68k_register_t)(M68K_REG_D0+reg));
        reference[15]=m68k_get_reg(NULL,M68K_REG_SR);
        assert(r32(l->mode)==mode && r16(l->reenter)==1);
        memcpy(g_ram,images[0],RAM_SIZE);g_os=1;g_lineage=LIN_CRACKED;g_retail_parity=g_inventory_flow=1;
        m68k_set_reg(M68K_REG_SR,0x2700|flags);
        for(unsigned reg=0;reg<15;reg++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+reg),0x12340000+reg);
        m68k_set_reg(M68K_REG_SP,STACK);w32(STACK,0x2bc56);w32(0x2fb1c,9);
        unsigned pc=mode==8?0x2cf18:0x2cf96;
        m68k_set_reg(M68K_REG_PC,pc);inventory_flow_hook(pc);
        for(unsigned reg=0;reg<15;reg++)assert(reference[reg]==m68k_get_reg(NULL,(m68k_register_t)(M68K_REG_D0+reg)));
        assert(reference[15]==m68k_get_reg(NULL,M68K_REG_SR));
        assert(r32(0x2fb1c)==mode && m68k_get_reg(NULL,M68K_REG_PC)==0x2bc56);
        cases++;
    }
    for(unsigned bad=0;bad<8;bad++) {
        memcpy(g_ram,images[0],RAM_SIZE);g_os=1;g_lineage=LIN_CRACKED;g_retail_parity=g_inventory_flow=1;
        m68k_set_reg(M68K_REG_SP,STACK);w32(STACK,0x2bc56);w32(0x2fb1c,9);
        m68k_set_reg(M68K_REG_PC,0x2cf18);
        if(bad==0)g_os=0;
        if(bad==1)g_lineage=LIN_UNKNOWN;
        if(bad==2)g_retail_parity=0;
        if(bad==3)g_inventory_flow=0;
        if(bad==4)w16(0x2cf18,0x4e71);
        if(bad==5)w16(0x2bc6a,0x4e71);
        if(bad==6)w32(STACK,STOP);
        if(bad==7)m68k_set_reg(M68K_REG_SP,STACK+1);
        inventory_flow_hook(0x2cf18);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2cf18 && r32(0x2fb1c)==9);
    }
    /* A coincidental return value must not be mistaken for an old inventory
     * frame; require a real inventory JSR at the terminal caller. */
    memcpy(g_ram,images[0],RAM_SIZE);g_os=1;g_lineage=LIN_CRACKED;g_retail_parity=g_inventory_flow=1;
    w32(STACK-4,0x2bc56);w32(STACK,STOP);m68k_set_reg(M68K_REG_SP,STACK-4);
    inventory_flow_hook(0x2bc30);assert(m68k_get_reg(NULL,M68K_REG_SP)==STACK-4);
    /* Retail synchronizes the previous mode when knight loot completes. */
    w32(0x2fb1c,9);w32(0x2fb04,1);inventory_flow_hook(0x2bd7e);assert(r32(0x2fb04)==9);
    printf("PASS %u original-retail register/CCR pairs; signature/option/stack guards and loot mode\n",cases);
}
int main(int argc,char **argv) {
    const char *dir=NULL,*log=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--dir")&&i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--log")&&i+1<argc)log=argv[++i];
    }
    if(!dir||!log)return 2;g_log=fopen(log,"w");assert(g_log);
    for(int i=0;i<2;i++) {
        char p[1400];snprintf(p,sizeof(p),"%s/%s.ram",dir,i?"retail":"port");
        FILE *f=fopen(p,"rb");assert(f);assert(fread(images[i],1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    for(int v=0;v<4;v++)for(int s=14;s<=16;s+=2)for(int take=0;take<2;take++)
        for(int key=0;key<2;key++)if(v||!key)for(int p=0;p<4;p++)for(int rotate=0;rotate<3;rotate+=2)
            run_case(v,s,take,key,rotate,p,1);
    for(int v=0;v<4;v++)for(int key=0;key<2;key++)if(v||!key)run_case(v,14,1,key,2,0,2);
    for(legacy_depth=1;legacy_depth<=8;legacy_depth*=2)
        for(int s=14;s<=16;s+=2)for(int key=0;key<2;key++)for(int take=0;take<2;take++)
            run_case(2,s,take,key,2,2,1);
    legacy_depth=0;preloot=1;
    for(int v=1;v<=2;v++)for(int s=14;s<=16;s+=2)for(int key=0;key<2;key++)run_case(v,s,1,key,2,2,1);
    preloot=0;transition_flags_and_guards();
    fclose(g_log);return 0;
}
