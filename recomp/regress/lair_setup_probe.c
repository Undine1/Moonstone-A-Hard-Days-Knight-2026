/* Initial-lair regression oracle. Never deploy; explicit scratch log required.
 * Runs original world initialization/dispatch/count code against retail data. */
#define main moonstone_main
#define moon_instr_hook production_hook
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

#define STOP 0x1ef000u
#define STACK 0x1ff000u
#define NODES 0x1a0000u
#define ITEMS 0x1a1000u
#define ACTOR 0x1a2000u
#define DAMAGE 0x1a2200u
static int native_mode, observe;
static unsigned snapshot_pc;
static const char *snapshot_path;
static unsigned step_count;

void moon_instr_hook(unsigned pc) {
    if (native_mode) {
        step_count++;
        if (native_mode == 1 || (native_mode == 3 && pc != 0x25ee0)) production_hook(pc);
        return;
    }
    if (observe && g_log && (pc == 0x25ec6 || pc == 0x25efe || pc == 0x259f8
        || pc == 0x25118 || pc == 0x251d6 || pc == 0x21366 || pc == 0x21852
        || pc == 0x21d00 || pc == 0x25a2e)) {
        unsigned node=r32(0x37178), base=r32(0x2dfda);
        fprintf(g_log,"LAIR-REVIEW pc=%06x fr=%d node=%06x index=%d selector=%u base=%d remaining=%d active=%u max=%u setup=%06x xy=%04x,%04x\n",
                pc,g_cur_frame,node,(int)(node-base)/20,r16(node+4),(int16_t)r16(node+6),
                (int16_t)r16(0x2e1e4),r16(0x2e1e8),r16(0x2e1e6),r32(0x2e1f0),r16(node+10),r16(node+12));
    }
    if (snapshot_path && pc == snapshot_pc) {
        assert(save_state(snapshot_path));
        fprintf(g_log,"REVIEW-SNAPSHOT pc=%06x fr=%d\n",pc,g_cur_frame);
        snapshot_path=NULL;
    }
    production_hook(pc);
}

static void cpu(unsigned pc) {
    for (unsigned i=0;i<SAVE_NREGS;i++) m68k_set_reg(SAVE_REGS[i],0);
    m68k_set_reg(M68K_REG_SR,0x2700);
    m68k_set_reg(M68K_REG_A7,STACK);
    m68k_set_reg(M68K_REG_PC,pc);
    m68k_set_irq(0);
    w32(STACK,STOP); w16(STOP,0x60fe);
    step_count=0;
}

static void finish(unsigned stop) {
    while (m68k_get_reg(NULL,M68K_REG_PC)!=stop && step_count<2000000) m68k_execute(1);
    if(m68k_get_reg(NULL,M68K_REG_PC)!=stop)
        fprintf(stderr,"Stopped at %06x wanted %06x after %u instructions, unmapped=%u\n",
                m68k_get_reg(NULL,M68K_REG_PC),stop,step_count,g_unmapped);
    assert(m68k_get_reg(NULL,M68K_REG_PC)==stop);
    assert(g_unmapped==0);
}

static unsigned short u16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }

static uint8_t *flat(const char *dir,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s-flat.sav",dir,name);
    FILE *file=fopen(path,"rb");assert(file);assert(fseek(file,20,SEEK_SET)==0);
    uint8_t *ram=malloc(RAM_SIZE);assert(ram);assert(fread(ram,1,RAM_SIZE,file)==RAM_SIZE);fclose(file);return ram;
}

static void write_ram(const char *dir,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ram",dir,name);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(g_ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
}

static void run_world(uint8_t *port,uint8_t *retail,unsigned mode,unsigned seed,unsigned players) {
    int ref=mode==3;
    memcpy(g_ram,ref?retail:port,RAM_SIZE);
    if(mode==2) {
        /* Test-only literal retail values, retaining the original table layout. */
        memcpy(g_ram+0x31116,retail+0x30eca,4);
        memcpy(g_ram+0x31120,retail+0x30ed4,2);
        memcpy(g_ram+0x31124,retail+0x30ed8,2);
    }
    w32(ref?0x2ddb2:0x2dfda,NODES);w32(ref?0x2ddb6:0x2dfde,ITEMS);
    w32(ref?0x38f68:0x391a8,seed);w16(ref?0x2ddfc:0x2e024,players);
    g_os=1;g_lineage=ref?LIN_RETAIL:LIN_CRACKED;g_retail_parity=mode!=3;g_lair_setup_fix=mode!=0;
    native_mode=mode==3?2:mode==2?3:1;
    g_unmapped=0;g_icount=0;
    if(g_retail_parity)apply_retail_parity();
    cpu(ref?0x25c3c:0x25b4c);finish(STOP);
}

static void tests(const char *dir) {
    uint8_t *port=flat(dir,"port"), *retail=flat(dir,"retail");
    uint8_t *baseline=malloc(RAM_SIZE),*world[4]; assert(baseline);
    for(unsigned i=0;i<4;i++){world[i]=malloc(RAM_SIZE);assert(world[i]);}
    const unsigned seeds[]={0xfffffde2,0xc88f,0xfffffffb,0xacfb,0x12345678,0xabcdef01,
        0x9e3779b9,0x31415926,0xdeadbeef,0x7fffffff,0x13579bdf,0x2468ace0};
    unsigned worlds=0,pairs=0;
    for(unsigned players=1;players<=4;players++) for(unsigned si=0;si<12;si++) {
        unsigned regs[SAVE_NREGS];uint8_t sword=0;uint8_t *fixed=malloc(RAM_SIZE);assert(fixed);
        for(unsigned mode=0;mode<4;mode++) {
            run_world(port,retail,mode,seeds[si],players);
            unsigned stride=mode==3?22:20, table=mode==3?0x30e82:0x310ce;
            if(mode==0){memcpy(baseline,g_ram,RAM_SIZE);sword=g_sword_created;
                for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);}
            if(mode==1 || mode==2) {
                unsigned diffs=0;
                for(unsigned a=0;a<RAM_SIZE;a++)if(g_ram[a]!=baseline[a]){
                    assert(a==0x31117||a==0x31119||a==0x31121||a==0x31125
                        ||a==NODES+18*20+5||a==NODES+18*20+7
                        ||a==NODES+20*20+7||a==NODES+21*20+7);diffs++;
                }
                assert(diffs==8 && sword==g_sword_created);
                for(unsigned i=0;i<SAVE_NREGS;i++)assert(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
                pairs++;
                if(mode==1)memcpy(fixed,g_ram,RAM_SIZE);
                else assert(!memcmp(fixed,g_ram,RAM_SIZE));
            }
            for(unsigned n=0;n<24;n++) {
                unsigned node=NODES+n*stride;
                assert(r32(node)==ITEMS+n*24);
                assert(r32(node+4)==r32(table+n*4));
                unsigned expected=mode>=1?0x30e82:0x310ce;
                const uint8_t *image=mode>=1?retail:port;
                assert(!memcmp(g_ram+node+4,image+expected+n*4,4));
                assert(r32(node+10)==r32(table+96+n*4));
                assert(r16(node+14)==r16(table+192+n*2));
                assert(r32(node+16)==r32(table+240+n*4));
                if(mode==3)assert(r16(node+20)==0);
            }
            if(players==1&&si==0){
                memcpy(world[mode],g_ram,RAM_SIZE);char name[32];snprintf(name,sizeof(name),"world-%u",mode);write_ram(dir,name);
            }
            worlds++;
        }
        free(fixed);
    }
    printf("PASS world initializers: %u full native executions; %u old/fixed and old/literal pairs differ only in 4 template and 4 copied bytes, all CPU registers and sword history equal\n",worlds,pairs);

    char path[1024];snprintf(path,sizeof(path),"%s/encounters.csv",dir);FILE *csv=fopen(path,"w");assert(csv);
    fputs("mode,index,strength,maxhp,weapon,selector,constructor,base,adjusted,simultaneous,damage_bin\n",csv);
    const unsigned strengths[]={1,3,4,8,16},health[]={20,29,30,59,60,89,90,120};
    unsigned comparisons=0;
    for(unsigned n=0;n<24;n++)for(unsigned s=0;s<5;s++)for(unsigned h=0;h<8;h++)for(unsigned w=0;w<4;w++) {
        unsigned results[4][4];
        for(unsigned mode=0;mode<4;mode++) {
            int ref=mode==3;unsigned node=NODES+n*(ref?22:20),current=ref?0x36f28:0x37178;
            unsigned actorvar=ref?0x2e9ac:0x2ebd0, count=ref?0x2dfbc:0x2e1e4;
            memcpy(g_ram,world[mode],RAM_SIZE);
            g_lineage=ref?LIN_RETAIL:LIN_CRACKED;g_retail_parity=mode!=3;g_lair_setup_fix=mode!=0;
            native_mode=mode==3?2:mode==2?3:1;g_unmapped=0;
            w32(current,node);w32(actorvar,ACTOR);
            unsigned selector=r16(node+4),setup=r32((ref?0x36f74:0x371c4)+selector);
            assert(setup!=0);
            cpu(ref?0x25ae8:0x259f8);finish(setup);
            assert(m68k_get_reg(NULL,M68K_REG_D0)==selector);
            assert(r32(ref?0x30b4e:0x30d9a)==2);
            assert(r32(ref?0x30b52:0x30d9e)==r32(node+16));
            /* Defaults chosen by each original setup routine, before difficulty. */
            const unsigned kinds[]={0x24,0x18,0x30,0x1c,4,0x40,0,0x20};
            const unsigned constructors[]={0x25400,0x24dec,0x25716,0x24f16,0x257e0,0x258ac,0x252d4,0x25050};
            const unsigned retail_constructors[]={0x254dc,0x24ec8,0x257f8,0x24ff2,0x258c2,0x2598e,0x253b0,0x2512c};
            unsigned kind=0;while(kind<8&&kinds[kind]!=selector)kind++;assert(kind<8);
            unsigned constructor=ref?retail_constructors[kind]:constructors[kind];
            w32(ref?0x2dfc8:0x2e1f0,constructor);
            unsigned replenish=selector==0x30?(ref?0x257ee:0x2570c):selector==4?(ref?0x258a8:0x257c6):0;
            w32(ref?0x2dfc4:0x2e1ec,replenish);
            w16(count,3);w16(count+2,selector==0x24||selector==0x18||selector==0x1c?2:1);
            w32(ACTOR+0x60,ACTOR+0x100);w32(ACTOR+0x2a,DAMAGE);w32(DAMAGE+8,4);
            w8(ACTOR+0x46,strengths[s]);w16(ACTOR+0x54,health[h]);w32(ACTOR+0x58,0x16+w);
            cpu(ref?0x25180:0x250a4);finish(STOP);
            results[mode][0]=selector;results[mode][1]=r16(count);results[mode][2]=r16(count+2);
            results[mode][3]=r16(ref?0x252f4:0x25218);
            fprintf(csv,"%u,%u,%u,%u,%u,%u,%x,%u,%d,%u,%u\n",mode,n,strengths[s],health[h],0x16+w,selector,constructor,r16(node+6),(int16_t)r16(count),r16(count+2),results[mode][3]);
        }
        assert(!memcmp(results[1],results[2],sizeof(results[1])));
        assert(!memcmp(results[2],results[3],sizeof(results[2])));
        if(n!=18&&n!=20&&n!=21)assert(!memcmp(results[0],results[3],sizeof(results[0])));
        comparisons++;
    }
    fclose(csv);
    printf("PASS encounter dispatch/count readers: %u four-build comparisons, %u native executions, all24 lairs / strength and HP thresholds / weapons; fixed=literal=retail; unaffected lairs=earlier\n",comparisons,comparisons*4);
    free(port);free(retail);free(baseline);for(unsigned i=0;i<4;i++)free(world[i]);native_mode=0;
}

/* Boundary controls use literal original/retail templates and skip the setup
 * hook, leaving already copied node records exactly as the save captured them. */
static void write_regs(const char *dir,const char *name) {
    char path[1200];snprintf(path,sizeof(path),"%s/%s.regs",dir,name);
    FILE *f=fopen(path,"wb");assert(f);
    unsigned regs[SAVE_NREGS];
    for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
    assert(fwrite(regs,1,sizeof(regs),f)==sizeof(regs));fclose(f);
}

static void boundary_tests(const char *dir,const char *out) {
    uint8_t *port=flat(dir,"port"),*retail=flat(dir,"retail"),*scratch=malloc(RAM_SIZE);
    assert(scratch);unsigned checks=0;
    const unsigned indices[]={18,20,21};
    /* Execute the real MOVE for every CCR combination and both saved values;
     * compare all CPU registers, memory and cycles to an independent literal. */
    for(unsigned k=0;k<3;k++)for(unsigned enabled=0;enabled<2;enabled++)
    for(unsigned saved=0;saved<2;saved++)for(unsigned ccr=0;ccr<32;ccr++) {
        unsigned index=indices[k],regs[2][SAVE_NREGS+1];
        for(unsigned reference=0;reference<2;reference++) {
            memcpy(g_ram,port,RAM_SIZE);w32(0x2dfda,NODES);
            memcpy(g_ram+0x310ceu +4*index,(saved?retail+0x30e82:port+0x310ce)+4*index,4);
            if(reference)memcpy(g_ram+0x310ceu +4*index,(enabled?retail+0x30e82:port+0x310ce)+4*index,4);
            cpu(0x25ee0);m68k_set_reg(M68K_REG_A0,NODES+20*index);
            m68k_set_reg(M68K_REG_A1,0x310ceu +4*index);m68k_set_reg(M68K_REG_D0,23-index);
            m68k_set_reg(M68K_REG_SR,0x2700|ccr);
            g_os=g_retail_parity=1;g_lair_setup_fix=enabled;g_lineage=LIN_CRACKED;
            native_mode=reference?3:1;g_unmapped=0;
            regs[reference][SAVE_NREGS]=m68k_execute(1);
            for(unsigned i=0;i<SAVE_NREGS;i++)regs[reference][i]=m68k_get_reg(NULL,SAVE_REGS[i]);
            assert(g_unmapped==0);
            if(!reference)memcpy(scratch,g_ram,RAM_SIZE);else assert(!memcmp(scratch,g_ram,RAM_SIZE));
        }
        assert(!memcmp(regs[0],regs[1],sizeof(regs[0])));checks++;
    }
    printf("PASS reader equivalence: %u native MOVE pairs; all CCR/registers/RAM/cycles equal\n",checks);
    /* Challenge every byte covered by the production signature, wrong lineage,
     * inactive switches, unrelated source/destination and malformed records. */
    const unsigned signature[]={0x25ec6,0x25ec7,0x25ec8,0x25ec9,0x25eca,0x25ecb,
        0x25ede,0x25edf,0x25ee0,0x25ee1,0x25ee2,0x25ee3,0x25ee4,0x25ee5,0x25ee6,0x25ee7,
        0x25ef4,0x25ef5,0x25ef6,0x25ef7,0x25ef8,0x25ef9,0x25efa,0x25efb,
        0x25efc,0x25efd,0x25efe,0x25eff};
    unsigned guards=0;
    for(unsigned k=0;k<3;k++)for(unsigned t=0;t<18+sizeof(signature)/sizeof(signature[0]);t++) {
        unsigned index=indices[k],pc=0x25ee0;
        memcpy(g_ram,port,RAM_SIZE);w32(0x2dfda,NODES);cpu(pc);
        m68k_set_reg(M68K_REG_A0,NODES+20*index);m68k_set_reg(M68K_REG_A1,0x310ceu +4*index);
        m68k_set_reg(M68K_REG_D0,23-index);g_os=g_retail_parity=g_lair_setup_fix=1;g_lineage=LIN_CRACKED;
        if(t==0)g_os=0;if(t==1)g_lineage=LIN_UNKNOWN;if(t==2)g_lineage=LIN_RETAIL;
        if(t==3)g_retail_parity=0;if(t==4)g_lair_setup_fix=0;if(t==5)pc+=4;
        if(t==6)m68k_set_reg(M68K_REG_A1,0x31117);
        if(t==7)m68k_set_reg(M68K_REG_A1,0x310ceu +4*index+4);
        if(t==8)m68k_set_reg(M68K_REG_A0,NODES+20*index+2);
        if(t==9)m68k_set_reg(M68K_REG_A0,NODES+20*24);
        if(t==10)m68k_set_reg(M68K_REG_D0,24-index);
        if(t==11)w32(0x2dfda,0);if(t==12)w32(0x2dfda,NODES+1);
        if(t==13)w32(0x2dfda,RAM_SIZE-20);
        if(t>=14&&t<18)w8(0x310ceu +4*index+(t-14),0xff);
        if(t>=18)w8(signature[t-18],r8(signature[t-18])^1);
        unsigned regs[SAVE_NREGS];for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        memcpy(scratch,g_ram,RAM_SIZE);g_icount=1;g_unmapped=0;production_hook(pc);
        assert(!memcmp(scratch,g_ram,RAM_SIZE));assert(g_unmapped==0);
        for(unsigned i=0;i<SAVE_NREGS;i++)assert(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));guards++;
    }
    printf("PASS guards: %u inactive/lineage/signature/source/node/count cases\n",guards);
    char path[1200];snprintf(path,sizeof(path),"%s/boundaries.tsv",out);
    FILE *manifest=fopen(path,"w");assert(manifest);unsigned total=0;
    for(unsigned scenario=0;scenario<3;scenario++) {
        unsigned source_enabled=scenario!=0,target_enabled=scenario!=2;
        run_world(port,retail,source_enabled?1:0,0x31415926,3);
        /* Re-enter just the original copy loop; loot/XP state remains real. */
        for(unsigned k=0;k<3;k++)memcpy(g_ram+0x310ceu +4*indices[k],
            (scenario==2?retail+0x30e82:port+0x310ce)+4*indices[k],4);
        memset(g_ram+NODES+4,0,4); /* prove the native copy actually runs */
        cpu(0x25ec6);m68k_set_reg(M68K_REG_A0,NODES);
        g_lair_setup_fix=source_enabled;g_retail_parity=1;native_mode=1;
        char names[64][64];unsigned count=0;
        while(m68k_get_reg(NULL,M68K_REG_PC)!=STOP) {
            unsigned pc=m68k_get_reg(NULL,M68K_REG_PC),d0=m68k_get_reg(NULL,M68K_REG_D0)&0xffff;
            int interesting=pc<0x25ee0 || pc==0x25efe || d0==5 || d0==3 || d0==2;
            if(interesting) {
                assert(count<64);snprintf(names[count],sizeof(names[count]),"s%u-%02u",scenario,count);
                snprintf(path,sizeof(path),"%s/%s.sav",out,names[count]);assert(save_state(path));count++;
            }
            m68k_execute(1);assert(g_unmapped==0);
        }
        for(unsigned n=0;n<count;n++) {
            snprintf(path,sizeof(path),"%s/%s.sav",out,names[n]);assert(load_state(path));
            g_stop=0;native_mode=3;step_count=0;g_retail_parity=1;
            unsigned pc=m68k_get_reg(NULL,M68K_REG_PC),cursor=m68k_get_reg(NULL,M68K_REG_A1);
            unsigned first=pc<0x25ee0?0:(cursor-0x310ce)/4;assert(first<=24);
            /* Source records whose MOVE already executed must remain untouched. */
            const uint8_t *table=target_enabled?retail+0x30e82:port+0x310ce;
            for(unsigned i=first;i<24;i++)memcpy(g_ram+0x310ceu +4*i,table+4*i,4);
            finish(STOP);memcpy(scratch,g_ram,RAM_SIZE);
            unsigned regs[SAVE_NREGS];for(unsigned i=0;i<SAVE_NREGS;i++)regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
            write_ram(out,names[n]);write_regs(out,names[n]);
            /* Pollute the host with another campaign before the warm restore. */
            run_world(port,retail,source_enabled?0:1,0xdeadbeef,4);
            assert(load_state(path));g_stop=0;g_lair_setup_fix=target_enabled;g_retail_parity=1;
            native_mode=1;step_count=0;finish(STOP);
            assert(!memcmp(scratch,g_ram,RAM_SIZE));
            for(unsigned i=0;i<SAVE_NREGS;i++)assert(regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
            fprintf(manifest,"%s\t%u\t%06x\t%u\n",names[n],target_enabled,pc,first);total++;
        }
    }
    fclose(manifest);free(port);free(retail);free(scratch);native_mode=0;
    printf("PASS boundaries: %u warm new/legacy/A-B saves match native literal controls; cold checks pending\n",total);
}

int main(int argc,char **argv) {
    const char *dir=NULL,*bounds=NULL,*resume=NULL;int n=1,have_log=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--review-tests")&&i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--lair-boundaries")&&i+1<argc)bounds=argv[++i];
        else if(!strcmp(argv[i],"--lair-resume")&&i+1<argc)resume=argv[++i];
        else if(!strcmp(argv[i],"--review-trace"))observe=1;
        else if(!strcmp(argv[i],"--review-snapshot")&&i+2<argc){snapshot_pc=(unsigned)strtoul(argv[++i],NULL,16);snapshot_path=argv[++i];}
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)have_log=1;argv[n++]=argv[i];}
    }
    if(!have_log){fprintf(stderr,"Explicit scratch --log required\n");return 2;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);if(rc)return rc;g_log=NULL;
    if(dir)tests(dir);
    if(bounds){assert(dir);boundary_tests(dir,bounds);}
    if(resume){g_stop=0;native_mode=1;step_count=0;finish(STOP);write_ram(resume,"cold-result");write_regs(resume,"cold-result");}
    return 0;
}
