/* Native disk-confirmation / multiplayer input checks. Test-only, never package. */
#define main moonstone_main
#define moon_instr_hook production_hook
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int returned, synthetic_seen, synthetic_lost;
static uint32_t sentinel=0x1f0000;
static const char *retail_image,*checkpoint;
static uint32_t save_pc;
static int saved;
void moon_instr_hook(unsigned pc) {
    if(pc==sentinel) { returned=1; m68k_end_timeslice(); return; }
    if(checkpoint && !saved && pc==save_pc) { assert(save_state(checkpoint));saved=1; }
    int reader=pc==mp_addr(0x2305a,0x23006);
    unsigned before=m68k_get_reg(NULL,M68K_REG_D1);
    production_hook(pc);
    if(reader && g_autoswap_armed && !g_autoswap_settle && (before&16)) {
        synthetic_seen++;
        if(!(m68k_get_reg(NULL,M68K_REG_D1)&16)) synthetic_lost++;
    }
}
static void setup(const char *fixture,int owner,int enemy,int manual,int held) {
    g_sdl_mode=0; assert(load_state(fixture));
    if(retail_image) {
        FILE *f=fopen(retail_image,"rb");assert(f && !fseek(f,20,SEEK_SET));
        assert(fread(g_ram,1,RAM_SIZE,f)==RAM_SIZE);assert(!fclose(f));
        g_lineage=LIN_RETAIL;
    } else g_lineage=LIN_CRACKED;
    g_os=1; g_sdl_mode=1; g_mp_practice=0;
    w16(mp_addr(0x2e024,0x2ddfc),4);w16(mp_addr(0x3051e,0x302d2),3);
    uint32_t base=mp_addr(0x2e7dc,0x2e5b4),pair=mp_addr(0x2e0bc,0x2de94);
    for(int p=0;p<4;p++)w32(base+p*0x84+0x36,p);
    if(enemy)w32(base+3*0x84+0x36,4);
    w32(pair,enemy?base+3*0x84:base+owner*0x84);w32(pair+4,base+owner*0x84);
    w32(mp_addr(0x2ebd0,0x2e9ac),base+owner*0x84);
    mp_reset(&g_mp);g_mp.players=4;g_mp.phase=MP_PLAY;g_mp.required=1u<<owner;g_mp.enrolled=15;
    for(int p=0;p<4;p++)g_mp.device[p]=100+p;
    g_mp_campaign=1;g_mp_context=MP_CAM_MAP;g_mp_ui_player=owner;g_mp_combatants=0;
    for(int p=0;p<4;p++)g_mp_input[p]=held?16:0;
    g_autoswap=!manual;g_autoswap_armed=!manual;g_autoswap_settle=0;
    g_fire=g_fire2=held;
    returned=synthetic_seen=synthetic_lost=0;saved=0;
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,0x1ef000);
    w32(0x1ef000,sentinel);w16(sentinel,0x4e71);
    m68k_set_reg(M68K_REG_PC,mp_addr(0x22fd0,0x22f7c));
}
int main(int argc,char **argv) {
    const char *log=NULL,*fixture=NULL;int expect_old=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--log") && i+1<argc)log=argv[++i];
        else if(!strcmp(argv[i],"--fixture") && i+1<argc)fixture=argv[++i];
        else if(!strcmp(argv[i],"--dataset") && i+1<argc)g_dataset=argv[++i];
        else if(!strcmp(argv[i],"--retail-image") && i+1<argc)retail_image=argv[++i];
        else if(!strcmp(argv[i],"--checkpoint") && i+1<argc)checkpoint=argv[++i];
        else if(!strcmp(argv[i],"--expect-old"))expect_old=1;
    }
    if(!log || !fixture)return 2;
    g_log=fopen(log,"w");assert(g_log);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    for(int owner=0;owner<4;owner++)for(int enemy=0;enemy<=(owner<3);enemy++)for(int held=0;held<2;held++) {
        setup(fixture,owner,enemy,0,held);m68k_execute(20000);
        if(expect_old && !held) {
            assert(!returned && synthetic_seen && synthetic_lost);
            printf("REPRO P%d enemy=%d: automatic fire overwritten, owner now P%d\n",owner+1,enemy,g_mp_ui_player+1);
        } else if(!expect_old) {
            assert(returned && !g_autoswap_armed);
            assert(g_mp_ui_player==owner && g_mp_context==MP_CAM_MAP && g_mp.required==(1u<<owner));
            printf("PASS P%d enemy=%d held=%d: automatic wait completes without changing ownership\n",owner+1,enemy,held);
        }
    }
    if(!expect_old) {
        for(int owner=0;owner<4;owner++) {
            setup(fixture,owner,0,0,1);g_autoswap_settle=3;
            for(int n=0;n<3;n++) {
                m68k_execute(2000);assert(!returned && g_autoswap_armed);
                autoswap_tick();
            }
            m68k_execute(2000);assert(returned && !g_autoswap_armed);
            setup(fixture,owner,0,1,0);m68k_execute(2000);assert(!returned);
            g_fire=g_fire2=1;g_mp_input[owner]=16;
            m68k_execute(2000);assert(!returned);
            g_fire=g_fire2=0;g_mp_input[owner]=0;
            m68k_execute(2000);assert(returned);
        }
        puts("PASS: all four owners retain the settle delay with held input; manual/native waits still require press and release");
        if(checkpoint) {
            const uint32_t sites[]={0x22fd0,0x22fd4,0x22fda,0x22fde};
            for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++) {
                save_pc=mp_addr(sites[i],sites[i]-0x54);
                setup(fixture,1,1,0,0);m68k_execute(20000);assert(returned && saved);
                assert(load_state(checkpoint));returned=0;
                m68k_execute(20000);assert(returned && !g_autoswap_armed);
            }
            puts("PASS: saves at both native press/release calls and comparisons resume and complete");
        }
    }
    fclose(g_log);return 0;
}
