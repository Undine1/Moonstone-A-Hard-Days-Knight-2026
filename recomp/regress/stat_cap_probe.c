/* Test-only native stat upgrade oracle. Never deploy; explicit --log required. */
#define SDL_MAIN_HANDLED
#define moon_instr_hook stat_production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>
#define ACTOR 0x100000u
#define ITEMS 0x100400u
#define STOP 0x1ef000u
static int oracle, edition;
static uint8_t *original[2], baseline[RAM_SIZE];
static const char *capture_dir;
static int capturing, finished, scene_override=-1;
static unsigned capture_count;
void moon_instr_hook(unsigned pc) {
    if(scene_override>=0 && pc==0x2bc0a) {
        m68k_set_reg(M68K_REG_D0,scene_override);scene_override=-1;
    }
    if(capture_dir && !finished) {
        if(pc==0x2cda6)capturing=1;
        if(capturing && ((pc>=0x2cda6 && pc<=0x2cdde)
            || (pc>=0x2d0b6 && pc<=0x2d15c)
            || (pc>=0x2cd6a && pc<=0x2cd7c))) {
            char path[1200];assert(capture_count<100);
            snprintf(path,sizeof(path),"%s/mid-%02u.sav",capture_dir,capture_count++);
            assert(save_state(path));fprintf(g_log,"STAT-BOUNDARY index=%u pc=%x\n",capture_count-1,pc);
        }
        if(capturing && (pc==0x2bca4 || pc==0x2bc56))finished=1;
    }
    if(!oracle || edition>=2)stat_production_hook(pc);
}
static void call(unsigned pc,unsigned a0) {
    m68k_set_reg(M68K_REG_SR,0x2700);
    for(int i=0;i<15;i++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_A0,a0);m68k_set_reg(M68K_REG_A7,0x1ff000);w32(0x1ff000,STOP);
    m68k_set_reg(M68K_REG_PC,pc);
    unsigned n=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=STOP) {
        if(++n>200000){fprintf(stderr,"timeout pc=%x entry=%x\n",m68k_get_reg(NULL,M68K_REG_PC),pc);abort();}
        m68k_execute(1);assert(!g_stop);
    }
}
static void prepare(int scene,int price,int xp,int str,int con,int end) {
    int ref=edition==1;
    memcpy(g_ram,original[ref],RAM_SIZE);g_os=edition>=2;g_lineage=LIN_CRACKED;g_retail_parity=1;
    g_stop=0;g_icount=0;g_sdl_mode=0;g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;m68k_set_irq(0);
    /* Flat oracle hunks place the unrelated knife code differently from the
     * live loader. Supply only its resident marker so the existing production
     * parity pass also installs the already-shipped >=5 test (earlier is ==5). */
    if(edition>=2){w16(0x41106,0x0628);apply_retail_parity();}
    g_stat_cap_fix=edition!=3;
    unsigned ports[]={0x2a17c,0x3f008,0x3ee32},refs[]={0x2a038,0x3ecec,0x3eb16};
    for(unsigned i=0;i<3;i++)w16(ref?refs[i]:ports[i],0x4e75);
    memset(g_ram+ACTOR,0,0x800);
    w32(ACTOR+0x60,ITEMS);w32(ACTOR+0x58,0x16);w32(ACTOR+0x5c,0x1b);
    w8(ACTOR+0x46,str);w8(ACTOR+0x47,con);w8(ACTOR+0x48,end);w8(ACTOR+0x49,3);
    w16(ACTOR+0x4e,xp);w16(ACTOR+0x50,20);
    w32(ref?0x2de94:0x2e0bc,ACTOR);w32(ref?0x2f8ca:0x2fb1c,scene);w16(ref?0x302dc:0x30528,price);
    call(ref?0x2d4ba:0x2d6e0,0);
    call(ref?0x2da1c:0x2dc42,0);
    assert(r32(ref?0x2f8b6:0x2fb08)==ACTOR);
    w32(ref?0x2e9a8:0x2ebcc,ACTOR);w32(ref?0x2f8ae:0x2fafe,ref?0x2fbf6:0x2fe44);
    w32(ref?0x390a8:0x392ea,0x181000);
    for(unsigned i=0;i<64;i++){w16(0x18100eu+i*10,8);w16(0x181010u+i*10,8);}
    unsigned site=ref?0x2a356:0x2a49a;assert(r16(site)==0x2079);w32(r32(site+2),0x184000);
    memset(g_ram+0x184000,0,3000);call(ref?0x2bf44:0x2c044,0);
    unsigned helper=ref?0x2cc36:0x2cd6a;
    w16(r32(helper+10),0x4e75);w16(0x3aa44,0x4e75);
    if(!ref)w16(r32(0x2d0d4),0x4e75);
    w16(ref?0x2bb9a:0x2bca4,0x4e75);w16(ref?0x2b802:0x2b92a,0x4e75);
    g_stat_cap_fix=1;
    memcpy(baseline,g_ram,RAM_SIZE);
}
static void matrix(const char *dir) {
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);oracle=1;
    for(int ref=0;ref<2;ref++){
        char p[1200];snprintf(p,sizeof(p),"%s/%s.sav",dir,ref?"retail":"port");
        FILE *f=fopen(p,"rb");assert(f);assert(!fseek(f,20,SEEK_SET));
        original[ref]=malloc(RAM_SIZE);assert(original[ref]);assert(fread(original[ref],1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
    unsigned cases=0,bad[4]={0};int stats[]={1,4,5,8},scenes[]={0,10};
    for(edition=0;edition<4;edition++)for(int sc=0;sc<2;sc++)for(int price=1;price<=3;price++)for(int sufficient=0;sufficient<2;sufficient++)
    for(int s=0;s<4;s++)for(int c=0;c<4;c++)for(int e=0;e<4;e++){
        int before[]={stats[s],stats[c],stats[e]},xp=sufficient?30:0;
        prepare(scenes[sc],price,xp,before[0],before[1],before[2]);
        unsigned seen=0;
        for(unsigned n=0;n<98;n++){
            memcpy(g_ram,baseline,RAM_SIZE);unsigned h=0x184000+n*24;
            if(!r16(h+4))break;unsigned field=r16(h+22);
            if(r16(h+20)!=3 || field<0x46 || field>0x48)continue;
            unsigned stat=field-0x46;if(seen&(1u<<stat))continue;seen|=1u<<stat;
            unsigned action=r32(h+16),flags=r16(r32(h+8)+8);
            call(edition==1?0x2cc4a:0x2cd7e,h);
            int gained=r8(ACTOR+field)-before[stat],expected=sufficient&&before[stat]<5;
            if(gained!=expected)bad[edition]++;
            if(edition>=2 && stat)assert(r32(h+16)==stat*4);
            if(edition>=1){
                const char *names[]={"Strength","Constitution","Endurance"};
                unsigned caption=r32(r32(h+8));assert(caption<RAM_SIZE-32);
                assert(strstr((char*)g_ram+caption,names[stat]));
            }
            assert(gained==0 || gained==1);assert(r16(ACTOR+0x4e)==xp-gained*price);
            for(int j=0;j<3;j++)if(j!=stat)assert(r8(ACTOR+0x46+j)==before[j]);
            fprintf(g_log,"STAT edition=%d scene=%d price=%d xp=%d before=%d,%d,%d field=%x action=%u flags=%x gain=%d expected=%d hp=%u\n",edition,scenes[sc],price,xp,before[0],before[1],before[2],field,action,flags,gained,expected,r16(ACTOR+0x54));cases++;
        }
        assert(seen==7);
    }
    assert(bad[0]>0 && bad[1]==0 && bad[2]==0 && bad[3]==0);
    printf("STAT cases=%u mismatches earlier=%u retail=%u current=%u cached=%u\n",cases,bad[0],bad[1],bad[2],bad[3]);
}
static void boundaries(void) {
    /* Fail closed on other modules and non-stat hotspots, even when the PC
     * coincides with one of our hook sites. Keep RAM and CPU state untouched. */
    unsigned sites[]={0x2c92e,0x2a564,0x2d0c2,0x2d0e6},cases=0;
    uint8_t *unchanged=malloc(RAM_SIZE);assert(unchanged);
    for(unsigned p=0;p<4;p++)for(int variant=0;variant<10;variant++){
        edition=3;prepare(0,1,30,2,4,5);
        unsigned h=0;
        for(unsigned n=0;n<98;n++)if(r16(0x184000+n*24+22)==0x47){h=0x184000+n*24;break;}
        assert(h && r32(h+16)==8);
        m68k_set_reg(M68K_REG_A0,sites[p]==0x2d0e6?ACTOR:h);
        m68k_set_reg(M68K_REG_A1,h);m68k_set_reg(M68K_REG_A2,h);
        m68k_set_reg(M68K_REG_D0,0x12345678);m68k_set_reg(M68K_REG_D1,0x47);
        m68k_set_reg(M68K_REG_PC,sites[p]);
        switch(variant){
        case 0:g_stat_cap_fix=0;break;
        case 1:g_retail_parity=0;break;
        case 2:g_os=0;break;
        case 3:g_lineage=LIN_RETAIL;break;
        case 4:g_lineage=LIN_UNKNOWN;break;
        case 5:w16(sites[p],0x4e71);break;
        case 6:w16(0x2c0ea,0x4e71);break;
        case 7:w16(h+22,0x4a);break; /* gold, not a stat */
        case 8:w16(h+20,12);break; /* offerings outside Stonehenge */
        case 9:w32(h+8,0x2fe6e);break; /* lives entry */
        }
        memcpy(unchanged,g_ram,RAM_SIZE);
        unsigned regs[19];
        for(int i=0;i<19;i++)regs[i]=m68k_get_reg(NULL,(m68k_register_t)(M68K_REG_D0+i));
        stat_cap_hook(sites[p]);
        assert(!memcmp(unchanged,g_ram,RAM_SIZE));
        for(int i=0;i<19;i++)assert(regs[i]==m68k_get_reg(NULL,(m68k_register_t)(M68K_REG_D0+i)));
        cases++;
    }
    /* Right-hand opponent/loot stat labels must never grant an XP upgrade. */
    for(unsigned field=0x47;field<=0x48;field++){
        edition=3;prepare(0,1,30,2,4,4);
        unsigned h=0;
        for(unsigned n=0;n<98;n++)if(r16(0x184000+n*24+22)==field){h=0x184000+n*24;break;}
        assert(h);w32(h+8,field==0x47?0x2ffda:0x2ffcc);
        assert(!(r16(0x2ffcc+8)&0x40) && !(r16(0x2ffda+8)&0x40));
        memcpy(unchanged,g_ram,RAM_SIZE);call(0x2cd7e,h);
        assert(!memcmp(unchanged+ACTOR,g_ram+ACTOR,0x84));cases++;
    }
    free(unchanged);printf("PASS: %u module, non-stat and opponent-panel boundaries\n",cases);
}
int main(int argc,char **argv) {
    const char *dir=NULL,*log=NULL;int n=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--stat-oracle")&&i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--stat-capture")&&i+1<argc)capture_dir=argv[++i];
        else if(!strcmp(argv[i],"--stat-scene")&&i+1<argc)scene_override=atoi(argv[++i]);
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=argv[i+1];argv[n++]=argv[i];}
    }
    assert(log);argv[n]=NULL;
    if(dir){g_log=fopen(log,"w");assert(g_log);matrix(dir);boundaries();fclose(g_log);return 0;}
    int rc=moonstone_main(n,argv);
    if(capture_dir)assert(finished && capture_count);
    return rc;
}
