/* Test-only original-instruction oracle. Never deploy; explicit scratch --log required. */
#define moon_instr_hook xp_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

#define XP_STOP 0x1ef000u
#define XP_STACK 0x1ff000u
#define XP_ITEMS 0x100800u
static unsigned snapshot_pc;
static const char *snapshot_path;
static int xp_trace;

void moon_instr_hook(unsigned pc) {
    if (snapshot_path && pc == snapshot_pc) {
        assert(save_state(snapshot_path));
        snapshot_path = NULL;
    }
    if (xp_trace && (pc == 0x21612 || pc == 0x21708 || pc == 0x21bf2 || pc == 0x40ee8)) {
        unsigned a = m68k_get_reg(NULL, M68K_REG_A0);
        fprintf(g_log, "XP-STEP pc=%06x fr=%d day=%u phase=%u actor=%06x xp=%u\n", pc,
                g_cur_frame, r16(0x2e0d0), r16(0x30396), a,
                a < RAM_SIZE-0x84 ? r16(a+0x4e) : 0);
    }
    xp_original_hook(pc);
}

static void finish(unsigned pc) {
    unsigned n=0;
    while (m68k_get_reg(NULL,M68K_REG_PC)!=pc && n++<300000) m68k_execute(1);
    if (m68k_get_reg(NULL,M68K_REG_PC)!=pc)
        fprintf(stderr,"XP stop=%06x actual=%06x\n",pc,m68k_get_reg(NULL,M68K_REG_PC));
    assert(m68k_get_reg(NULL,M68K_REG_PC)==pc);
}

static unsigned roster(int ref) { return ref?0x2e5b4u:0x2e7dcu; }
static unsigned active(int ref) { return ref?0x2e9acu:0x2ebd0u; }
static unsigned rng(int ref) { return ref?0x38f68u:0x391a8u; }

static void setup(const uint8_t *ram, int ref, unsigned mask, unsigned seed) {
    memcpy(g_ram,ram,RAM_SIZE);
    memset(g_ram+roster(ref),0,5*0x84);
    memset(g_ram+XP_ITEMS,0,5*0x30);
    g_os=1;g_lineage=ref?LIN_RETAIL:LIN_CRACKED;
    g_retail_parity=!ref;g_ai_xp_fix=1;g_sword_created=0;
    w16(0x2e8c6,0); /* retail's separate sword-generation history */
    for (unsigned i=0;i<5;i++) {
        unsigned a=roster(ref)+i*0x84;
        w32(a+0x36,i==4?5:((mask>>i)&1)?4:i);
        w8(a+0x46,1);w8(a+0x47,1);w8(a+0x48,1);w8(a+0x49,5);
        w16(a+0x4a,10);w8(a+0x4d,0x0c);w16(a+0x50,20);w16(a+0x54,20);
        w32(a+0x58,0x16);w32(a+0x5c,0x1b);w32(a+0x60,XP_ITEMS+i*0x30);
    }
    w32(active(ref),roster(ref)+0x84);w32(rng(ref),seed);
    w32(ref?0x37cc0:0x37f00,0xffffffffu);
    w32(XP_STACK,XP_STOP);w16(XP_STOP,0x60fe);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_A7,XP_STACK);
    m68k_set_reg(M68K_REG_A0,roster(ref));m68k_set_reg(M68K_REG_A1,roster(ref)+0x84);
}

static void call(unsigned pc) {
    m68k_set_reg(M68K_REG_A7,XP_STACK);w32(XP_STACK,XP_STOP);
    m68k_set_reg(M68K_REG_PC,pc);finish(XP_STOP);
}

static void checks(const char *dir,const char *retail) {
    char path[1024];snprintf(path,sizeof(path),"%s/current.sav",dir);assert(load_state(path));
    uint8_t *current=malloc(RAM_SIZE),*reference=malloc(RAM_SIZE);
    assert(current&&reference);memcpy(current,g_ram,RAM_SIZE);
    assert(load_state(retail));memcpy(reference,g_ram,RAM_SIZE);
    const uint16_t values[]={0,1,2,0x7ffe,0x7fff,0xfffe,0xffff};
    unsigned ccr_cases=0;
    for(unsigned kind=0;kind<2;kind++)for(unsigned v=0;v<7;v++)for(unsigned sr=0;sr<32;sr++) {
        unsigned got[2][2];
        for(int ref=0;ref<2;ref++) {
            setup(ref?reference:current,ref,1,1);unsigned a=roster(ref);
            w16(a+0x4e,values[v]);m68k_set_reg(M68K_REG_SR,0x2700|sr);
            m68k_set_reg(M68K_REG_PC,kind?(ref?0x21c06:0x21bf2):(ref?0x2173e:0x21708));
            m68k_execute(1);
            got[ref][0]=r16(a+0x4e);got[ref][1]=m68k_get_reg(NULL,M68K_REG_SR);
        }
        if(memcmp(got[0],got[1],sizeof(got[0])))
            fprintf(stderr,"CCR kind=%u xp=%04x sr=%02x current=%04x/%04x retail=%04x/%04x\n",
                    kind,values[v],sr,got[0][0],got[0][1],got[1][0],got[1][1]);
        assert(!memcmp(got[0],got[1],sizeof(got[0])));ccr_cases++;
    }
    printf("PASS: %u periodic/victory result and CCR comparisons against native retail\n",ccr_cases);

    /* Execute the actual day caller and roster/lives gates for twelve days.
     * Only Math's gift routine is stubbed in this cadence-isolation matrix. */
    unsigned days=0;
    for(unsigned mask=0;mask<16;mask++)for(unsigned phase=0;phase<8;phase++)
    for(unsigned first=0;first<4;first++) {
        uint8_t results[2][5*0x84];
        for(int ref=0;ref<2;ref++) {
            setup(ref?reference:current,ref,mask,31);
            w16(ref?0x2a740:0x2a884,0x4e75);
            w16(ref?0x2dea8:0x2e0d0,first);w16(ref?0x30152:0x30396,phase);
            w16(ref?0x30150:0x30394,0);
            for(unsigned i=0;i<5;i++) {
                unsigned a=roster(ref)+i*0x84;
                w8(a+0x49,i==1?0:i==2?255:5);w16(a+0x4a,0xffff);
                w16(a+0x50,10);w8(a+0x53,70);w8(a+0x52,3);
            }
            for(unsigned day=1;day<=12;day++) {
                call(ref?0x2164a:0x21612);days++;
                unsigned awards=(first+day)/4;
                for(unsigned i=0;i<5;i++)
                    assert(r16(roster(ref)+i*0x84+0x4e)==((i==0||i==3)&&((mask>>i)&1)?awards:0));
                assert(r16(ref?0x2dea8:0x2e0d0)==(first+day)%4);
                assert(r16(ref?0x30152:0x30396)==(phase+awards)%8);
                assert(r32(active(ref))==roster(ref)+0x84);
                for(unsigned i=0;i<5*0x30;i++)assert(r8(XP_ITEMS+i)==0);
            }
            memcpy(results[ref],g_ram+roster(ref),sizeof(results[ref]));
        }
        assert(!memcmp(results[0],results[1],sizeof(results[0])));
    }
    printf("PASS: %u original day transitions; all ownership masks, moon phases, starting counters, dead/retired AI, human/Guardian exclusions\n",days);

    /* No gift stubs here: retain the original random item/stat/gold rewards. */
    for(unsigned seed=1;seed<=128;seed++) {
        uint8_t records[2][5*0x84],items[2][5*0x30];unsigned random[2];
        for(int ref=0;ref<2;ref++) {
            setup(ref?reference:current,ref,15,seed*0x12345u);
            call(ref?0x21704:0x216ce);
            memcpy(records[ref],g_ram+roster(ref),sizeof(records[ref]));
            memcpy(items[ref],g_ram+XP_ITEMS,sizeof(items[ref]));random[ref]=r32(rng(ref));
            for(unsigned i=0;i<4;i++)assert(r16(roster(ref)+i*0x84+0x4e)==1);
        }
        if(memcmp(records[0],records[1],sizeof(records[0]))||memcmp(items[0],items[1],sizeof(items[0])))
            fprintf(stderr,"gift seed=%u\n",seed);
        assert(!memcmp(records[0],records[1],sizeof(records[0])));
        assert(!memcmp(items[0],items[1],sizeof(items[0]))&&random[0]==random[1]);
    }
    puts("PASS: 128 complete retail/current periodic loops, including unmodified random gifts and inventories");

    unsigned stat_cases=0;
    for(unsigned players=1;players<=4;players++)for(unsigned xp=0;xp<7;xp++)for(unsigned cap=0;cap<8;cap++) {
        uint8_t result[2][0x84];
        unsigned price=players==1?3:players==2?2:1;
        for(int ref=0;ref<2;ref++) {
            setup(ref?reference:current,ref,1,players*123+xp*7+cap);
            unsigned a=roster(ref);w32(active(ref),a);w16(a+0x4e,xp);
            for(unsigned i=0;i<3;i++)w8(a+0x46+i,((cap>>i)&1)?5:1);
            w16(ref?0x2ddfc:0x2e024,players);call(ref?0x22b0a:0x22b68);
            assert(r16(ref?0x302dc:0x30528)==price);
            call(ref?0x40ac0:0x40eba);
            assert(r16(a+0x4e)==xp-((xp>=price&&cap!=7)?price:0));
            unsigned total=0;for(unsigned i=0;i<3;i++)total+=r8(a+0x46+i)-(((cap>>i)&1)?5:1);
            assert(total==((xp>=price&&cap!=7)?1:0));
            memcpy(result[ref],g_ram+a,0x84);
        }
        assert(!memcmp(result[0],result[1],0x84));stat_cases++;
    }
    printf("PASS: %u original stat-spending comparisons, all player-count prices and capped/eligible stats\n",stat_cases);

    const unsigned goods[]={0,2,4,6,8,10,12,14,16,18};
    for(unsigned item=0;item<10;item++)for(unsigned dead=0;dead<2;dead++) {
        uint8_t record[2][4*0x84],items[2][5*0x30];
        for(int ref=0;ref<2;ref++) {
            setup(ref?reference:current,ref,1,1);unsigned a=roster(ref),l=a+0x84;
            w16(a+0x4e,2);w8(l+0x49,dead?0:2);w8(XP_ITEMS+0x30+goods[item],1);
            m68k_set_reg(M68K_REG_PC,ref?0x21ba4:0x21b90);
            finish(ref?0x21c18:0x21bfc);
            assert(r16(a+0x4e)==3);
            memcpy(record[ref],g_ram+a,sizeof(record[ref]));memcpy(items[ref],g_ram+XP_ITEMS,sizeof(items[ref]));
        }
        assert(!memcmp(record[0],record[1],sizeof(record[0]))&&!memcmp(items[0],items[1],sizeof(items[0])));
    }
    puts("PASS: 20 native AI victory branches and ordinary loot transfers match retail");

    /* Check the unchanged token policy at both actual merge sites. No opcode
     * patches: direct hook inspection, then native merge for allowed winners. */
    unsigned token_cases=0;
    for(unsigned site=0;site<2;site++)for(unsigned slot=0;slot<7;slot++)
    for(unsigned kind=0;kind<3;kind++)for(unsigned item=0x14;item<=0x16;item+=2) {
        setup(current,0,0,1);unsigned a=roster(0),pc=site?0x215e4:0x215c2;
        w32(a+0x36,slot);w8(a+0x4d,kind==0?0x0c:kind==1?0x14:0x28);
        w8(XP_ITEMS+item,1);w8(XP_ITEMS+0x30+item,2);
        m68k_set_reg(M68K_REG_A2,XP_ITEMS+0x30);m68k_set_reg(M68K_REG_A3,XP_ITEMS);
        m68k_set_reg(M68K_REG_D0,item);m68k_set_reg(M68K_REG_PC,pc);
        int blocked=item==0x16&&(slot==4||slot==6||(slot==5&&kind!=0));
        int count=g_msleak_blocked;xp_original_hook(pc);
        assert(g_msleak_blocked-count==blocked);
        assert(m68k_get_reg(NULL,M68K_REG_PC)==(blocked?(site?0x21578:0x21596):pc));
        if(!blocked)finish(site?0x215f4:0x215d2);
        assert(r8(XP_ITEMS+item)==(blocked?1:3)&&r8(XP_ITEMS+0x30+item)==(blocked?2:0));
        token_cases++;
    }
    printf("PASS: %u actual key/Moonstone merge cases retain human, AI, Guardian and dragon policy\n",token_cases);

    /* Prove the terminology with the real token grant and human XP consumer.
     * Start after input flushing/turn selection; all grant instructions execute. */
    for(unsigned mask=0;mask<16;mask++)for(unsigned seed=1;seed<=16;seed++) {
        setup(current,0,0,seed*12345);unsigned a=roster(0);
        w32(active(0),a);w16(a+0x4e,123);w8(XP_ITEMS+0x14,15);w8(XP_ITEMS+0x16,mask);
        call(0x405ee);
        unsigned bit=m68k_get_reg(NULL,M68K_REG_D0);
        assert(bit<4&&r8(XP_ITEMS+0x16)==(mask|(1u<<bit)));
        assert(r16(a+0x4e)==123&&r8(XP_ITEMS+0x14)==15);
        w16(0x30528,3);m68k_set_reg(M68K_REG_A0,a);m68k_set_reg(M68K_REG_PC,0x2d0f8);
        finish(0x2d106);
        assert(r16(a+0x4e)==120&&r8(XP_ITEMS+0x16)==(mask|(1u<<bit))&&r8(XP_ITEMS+0x14)==15);
    }
    puts("PASS: 256 native Moonstone grants/human XP deductions prove the fields are independent");

    for(unsigned mode=0;mode<13;mode++) {
        setup(current,0,1,1);unsigned a=roster(0);w16(a+0x4e,5);
        if(mode==0)g_os=0;if(mode==1)g_retail_parity=0;if(mode==2)g_ai_xp_fix=0;
        if(mode==3)g_lineage=LIN_RETAIL;if(mode==4)g_lineage=LIN_UNKNOWN;
        if(mode==5)w16(0x21708,0x4e71);if(mode==6)w16(0x21710,0);
        if(mode==7)w32(a+0x36,0);if(mode==8)w8(a+0x49,0);if(mode==9)w8(a+0x49,255);
        if(mode==10)m68k_set_reg(M68K_REG_A0,0);if(mode==11)m68k_set_reg(M68K_REG_A0,RAM_SIZE-2);
        if(mode==12)m68k_set_reg(M68K_REG_A0,a+1);
        m68k_set_reg(M68K_REG_PC,0x21708);m68k_set_reg(M68K_REG_SR,0x271f);xp_original_hook(0x21708);
        assert(r16(a+0x4e)==5&&m68k_get_reg(NULL,M68K_REG_SR)==0x271f);
    }
    puts("PASS: 13 periodic mode, lineage, code, actor and lives guards");

    /* Serialize exactly before/after native instructions; resume both in-process
     * and (by the Python driver) in a fresh production process. */
    const unsigned boundaries[]={0x21708,0x2170c,0x21712,0x21716,0x21bf2,0x21bf8,0x21bfc};
    for(unsigned n=0;n<sizeof(boundaries)/sizeof(boundaries[0]);n++) {
        unsigned pc=boundaries[n],victory=n>=4;
        setup(current,0,1,0x12345);unsigned a=roster(0);w16(a+0x4e,2);
        if(victory) {
            /* Resume the existing loot return boundary without opening map UI. */
            w16(0x21bfc,0x4ef9);w32(0x21bfe,XP_STOP);
            m68k_set_reg(M68K_REG_PC,0x21b90);
        } else m68k_set_reg(M68K_REG_PC,0x216ce);
        finish(pc);snprintf(path,sizeof(path),"%s/boundary-%x.sav",dir,pc);assert(save_state(path));
        finish(XP_STOP);assert(r16(a+0x4e)==3);
        w16(a+0x4e,99);assert(load_state(path));finish(XP_STOP);assert(r16(a+0x4e)==3);
    }
    puts("PASS: seven instruction-boundary warm saves award once; files prepared for production cold loads");
    free(current);free(reference);
}

int main(int argc,char **argv) {
    const char *dir=NULL,*retail=NULL;int n=1,log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--xp-tests")&&i+2<argc){dir=argv[++i];retail=argv[++i];}
        else if(!strcmp(argv[i],"--xp-trace"))xp_trace=1;
        else if(!strcmp(argv[i],"--xp-snapshot")&&i+2<argc){snapshot_pc=(unsigned)strtoul(argv[++i],NULL,16);snapshot_path=argv[++i];}
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=1;argv[n++]=argv[i];}
    }
    if(!log){fprintf(stderr,"An explicit scratch --log is required.\n");return 2;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);if(rc)return rc;
    if(dir){g_log=NULL;xp_trace=0;checks(dir,retail);}
    return 0;
}
