/* Test-only original number formatting and live inventory observer.
 * Never deploy. --log must always name a scratch file. */
#define SDL_MAIN_HANDLED
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int oracle, fixed;
static const char *capture;
static int captured;
static int scene_override=-1;
static uint8_t originals[2][RAM_SIZE];
void moon_instr_hook(unsigned pc) {
    if (scene_override>=0 && pc==0x2bc0a) {
        m68k_set_reg(M68K_REG_D0,(unsigned)scene_override);scene_override=-1;
    }
    if (capture && !captured && pc == 0x2c24e) {
        assert(save_state(capture));captured=1;
    }
    if (!oracle || fixed) production_hook(pc);
    if (!oracle && pc == 0x2c254) {
        unsigned a=r32(0x2ebcc), field=r16(0x2c040);
        fprintf(g_log,"STAT-DISPLAY actor=%x field=%x actual=%u text=%.3s\n",
            a,field,r8(a+field),g_ram+0x392ee);
    }
}

static void matrix(const char *dir) {
    for (int e=0;e<2;e++) {
        char path[1400];snprintf(path,sizeof(path),"%s/%s.ram",dir,e?"retail":"port");
        FILE *f=fopen(path,"rb");assert(f);
        assert(fread(originals[e],1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
    oracle=1;m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    unsigned widths[]={0,17,195,255,256,300,511,512};
    unsigned values[]={0,1,2,4,5,8,127,128,254,255};
    unsigned cases=0,guards=0;
    /* Original earlier/retail, fixed earlier/retail, disabled fix and parity
     * disabled. The last case proves this retail bug repair is independent. */
    for (int mode=0;mode<6;mode++) for (unsigned k=0;k<8;k++)
    for (unsigned field=0x46;field<=0x48;field++) for (unsigned v=0;v<10;v++) {
        int e=mode==1 || mode==3;fixed=mode>=2;
        memcpy(g_ram,originals[e],RAM_SIZE);g_os=fixed;g_stop=0;
        g_lineage=e?LIN_RETAIL:LIN_CRACKED;g_retail_parity=mode!=5;g_stat_display_fix=mode!=4;
        g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;
        unsigned ret=e?0x2a1e0:0x2a324,loop=e?0x2c124:0x2c22c,stop=e?0x2c14c:0x2c254;
        unsigned width=e?0x2a284:0x2a3c8,slot=e?0x2bf40:0x2c040,who=e?0x2e9a8:0x2ebcc,text=e?0x390ac:0x392ee;
        w16(width,widths[k]);w16(slot,field);w32(who,0x180000);
        memset(g_ram+0x180000,0x5a,0x84);w8(0x180000+field,values[v]);
        uint8_t before[0x84];memcpy(before,g_ram+0x180000,sizeof(before));
        m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
        for (int i=0;i<15;i++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+i),0);
        m68k_set_reg(M68K_REG_SP,0x1ff000);w32(0x1ff000,loop);m68k_set_reg(M68K_REG_PC,ret);
        unsigned steps=0;
        while (m68k_get_reg(NULL,M68K_REG_PC)!=stop) {
            assert(++steps<1000);m68k_execute(1);assert(!g_stop);
        }
        unsigned displayed=(unsigned)strtoul((char*)g_ram+text,NULL,10);
        unsigned expected=(fixed && g_stat_display_fix)?values[v]:(widths[k]&0xff00u)|values[v];
        assert(displayed==expected && !memcmp(before,g_ram+0x180000,sizeof(before)));
        fprintf(g_log,"CASE mode=%d width=%u field=%x actual=%u displayed=%u\n",mode,widths[k],field,values[v],displayed);
        cases++;
    }
    for (int e=0;e<2;e++) for (int test=0;test<9;test++) {
        memcpy(g_ram,originals[e],RAM_SIZE);g_os=1;g_stat_display_fix=1;
        g_lineage=e?LIN_RETAIL:LIN_CRACKED;g_retail_parity=1;
        unsigned pc=e?0x2c146:0x2c24e;
        if(test==1)g_os=0;
        if(test==2)g_stat_display_fix=0;
        if(test==3)g_lineage=LIN_UNKNOWN;
        if(test==4)pc+=2;
        if(test==5)w16(pc,0x4e71);
        if(test==6)w32(pc+2,0x2a000);
        if(test==7)w16(pc-20,0x3030);
        if(test==8)g_lineage=e?LIN_CRACKED:LIN_RETAIL;
        m68k_set_reg(M68K_REG_D0,0x12345601);m68k_set_reg(M68K_REG_D1,0x47);
        m68k_set_reg(M68K_REG_SR,0x271f);
        stat_display_hook(pc);
        assert(m68k_get_reg(NULL,M68K_REG_D0)==(test?0x12345601u:1u));
        assert(m68k_get_reg(NULL,M68K_REG_D1)==0x47 && m68k_get_reg(NULL,M68K_REG_SR)==0x271f);
        guards++;
    }
    printf("PASS: %u original/fixed stat formatting cases; %u scope/flags guards\n",cases,guards);
}

int main(int argc,char **argv) {
    const char *dir=NULL,*log=NULL;int n=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--stat-display-oracle")&&i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--stat-display-capture")&&i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--stat-display-scene")&&i+1<argc)scene_override=atoi(argv[++i]);
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=argv[i+1];argv[n++]=argv[i];}
    }
    assert(log);argv[n]=NULL;
    if(!dir)return moonstone_main(n,argv);
    g_log=fopen(log,"w");assert(g_log);matrix(dir);fclose(g_log);return 0;
}
