/* Dragon-fire timing/save regression probe. Never deploy; scratch --log required. */
#define moon_instr_hook animation_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

static int oracle, steps, limit=256, tracing, updates, captures;
static unsigned record;
static char parts[2048];
static const char *capture;

void moon_instr_hook(unsigned pc) {
    int retail=g_lineage==LIN_RETAIL;
    unsigned entry=retail?0x289e8:0x28a7c;
    unsigned pose=retail?0x28a4c:0x28ae0;
    unsigned done=retail?0x28cf0:0x28dca;
    if(oracle && pc==0x1e0000) {
        unsigned d=m68k_get_reg(NULL,M68K_REG_A1);
        if(++steps>limit || (steps>1 && !r8(d+1))) {
            halt("animation oracle complete"); return;
        }
    }
    if(g_log && pc==entry) {
        unsigned d=m68k_get_reg(NULL,M68K_REG_A1), s=r32(d+2);
        int selected=oracle || (s>=0x32018 && s<0x3208a);
        record=selected?d:0;
        parts[0]=0;
        if(selected) {
            unsigned a=r32(d+0x18);
            fprintf(g_log,"ANIM-BEGIN frame=%d update=%d display=%06x actor=%06x cursor=%06x hp=%d bank=%06x active=%u\n",
                g_cur_frame,++updates,d,a,s,(int16_t)r16(a+0x50),r32(d+0x1c),r8(d+1));
            if(!oracle && capture && !captures++) {
                if(!save_state(capture)) halt("capture failed");
            }
        }
    }
    if(record && pc==pose) {
        unsigned s=m68k_get_reg(NULL,M68K_REG_A6);
        size_t n=strlen(parts);
        if(n+24<sizeof(parts))snprintf(parts+n,sizeof(parts)-n,"%s%02x%02x%02x%02x%04x",n?"/":"",
            r8(s),r8(s+1),r8(s+2),r8(s+3),r16(s+4));
    }
    if(record && pc==done) {
        unsigned d=record,a=r32(d+0x18),w=r32(d+0x24);
        fprintf(g_log,"ANIM-END frame=%d update=%d cursor=%06x delay=%u enabled=%u loop=%u looping=%u bank=%06x actorbank=%06x active=%u hp=%d parts=%s\n",
            g_cur_frame,updates,r32(d+2),r8(w),r8(w+1),r8(w+6),r8(w+7),r32(d+0x1c),r32(a+0x26),r8(d+1),(int16_t)r16(a+0x50),parts);
        record=0;
    }
    if(tracing && (pc==0x26652 || pc==0x267a4 || pc==0x213f4 || pc==0x21380))
        fprintf(g_log,"ANIM-EVENT frame=%d pc=%06x a0=%06x a1=%06x\n",g_cur_frame,pc,m68k_get_reg(NULL,M68K_REG_A0),m68k_get_reg(NULL,M68K_REG_A1));
    animation_original_hook(pc);
}

static void state_regs(unsigned *regs) {
    for(unsigned r=0;r<21;r++)regs[r]=m68k_get_reg(NULL,(m68k_register_t)r);
}
static void native_finish(void) {
    unsigned n=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=0x1e0000 || r8(0x2eca1)) {
        assert(++n<500000);m68k_execute(1);assert(!g_stop);
    }
}
static void fresh(const char *path,int enabled) {
    assert(load_state(path));g_os=1;g_lineage=LIN_CRACKED;g_stop=0;
    g_dragon_fire_fix=enabled;g_retail_parity=1;g_icount=1;
    m68k_set_irq(0);m68k_set_reg(M68K_REG_SR,0x2700);
}
static void unit_checks(const char *input,const char *out) {
    uint8_t *expected[2]={malloc(RAM_SIZE),malloc(RAM_SIZE)},*scratch=malloc(RAM_SIZE);
    unsigned regs[2][21];assert(expected[0]&&expected[1]&&scratch);
    for(unsigned enabled=0;enabled<2;enabled++) {
        fresh(input,enabled);native_finish();memcpy(expected[enabled],g_ram,RAM_SIZE);
        state_regs(regs[enabled]);
    }
    for(unsigned count=1;count<=4;count+=3)for(unsigned ccr=0;ccr<32;ccr++) {
        unsigned result[2][22];
        for(unsigned reference=0;reference<2;reference++) {
            fresh(input,1);m68k_set_reg(M68K_REG_A6,0x3204e);
            m68k_set_reg(M68K_REG_A5,0x1e3000);m68k_set_reg(M68K_REG_PC,0x28f66);
            m68k_set_reg(M68K_REG_SR,0x2700|ccr);w8(0x3204f,reference?1:count);
            if(reference)g_lineage=LIN_UNKNOWN; /* literal native MOVE control */
            result[reference][21]=(unsigned)m68k_execute(1);state_regs(result[reference]);
            if(!reference)memcpy(scratch,g_ram,RAM_SIZE);
            else assert(!memcmp(scratch,g_ram,RAM_SIZE));
        }
        assert(!memcmp(result[0],result[1],sizeof(result[0])));
    }
    puts("PASS:64 native reader/control pairs; all CCR bits, registers, RAM and cycles agree");

    for(unsigned test=0;test<43;test++) {
        fresh(input,1);m68k_set_reg(M68K_REG_A6,0x3204e);
        m68k_set_reg(M68K_REG_A5,0x1e3000);m68k_set_reg(M68K_REG_PC,0x28f66);
        unsigned pc=0x28f66;
        if(test==0)g_os=0;if(test==1)g_lineage=LIN_UNKNOWN;if(test==2)g_lineage=LIN_RETAIL;
        if(test==3)g_retail_parity=0;if(test==4)g_dragon_fire_fix=0;
        if(test==5)m68k_set_reg(M68K_REG_A6,0x32050);if(test==6)pc+=2;
        if(test>=7) {
            unsigned i=test-7,at=i<14?0x28f62+i:0x3204a+i-14;
            w8(at,r8(at)^1);
        }
        memcpy(scratch,g_ram,RAM_SIZE);unsigned a[21],b[21];state_regs(a);
        animation_original_hook(pc);state_regs(b);
        assert(!memcmp(scratch,g_ram,RAM_SIZE)&&!memcmp(a,b,sizeof(a)));
    }
    puts("PASS:43 disabled, foreign, wrong-reader and signature-byte guards");

    /* Save every selected reader/countdown/loop boundary, including each old
     * repeat. A legacy loop already loaded into workspace must finish intact. */
    const unsigned pcs[]={0x28f62,0x28f66,0x28f6c,0x28f72,0x28f7a,0x28f80,
        0x28ee0,0x28f08,0x28f0e,0x28f16,0x28cda,0x28cf6,0x28d34,0x28d44,
        0x28d4a,0x28d50,0x28d5a,0x28d60};
    FILE *manifest;char path[1200];snprintf(path,sizeof(path),"%s/boundaries.tsv",out);
    manifest=fopen(path,"w");assert(manifest);
    unsigned total=0;
    for(unsigned enabled=0;enabled<2;enabled++) {
        fresh(input,enabled);unsigned n=0,committed=0,count=0;
        unsigned keys[160][4],want[160];char names[160][48];
        while(m68k_get_reg(NULL,M68K_REG_PC)!=0x1e0000 || r8(0x2eca1)) {
            assert(++n<500000);
            unsigned pc=m68k_get_reg(NULL,M68K_REG_PC),a6=m68k_get_reg(NULL,M68K_REG_A6);
            unsigned cursor=r32(0x2eca2),match=0;
            for(unsigned i=0;i<sizeof(pcs)/sizeof(pcs[0]);i++)if(pc==pcs[i])match=1;
            if(match && cursor>=0x3204e && cursor<=0x3205a && a6>=0x3204e && a6<=0x32058) {
                unsigned key[]={pc,cursor,r8(0x1e3006),r8(0x1e3000)},seen=0;
                for(unsigned i=0;i<count;i++)if(!memcmp(keys[i],key,sizeof(key)))seen=1;
                if(!seen) {
                    assert(count<160);memcpy(keys[count],key,sizeof(key));
                    want[count]=enabled||!committed;
                    snprintf(names[count],sizeof(names[count]),"%s-%03u.sav",enabled?"new":"legacy",count);
                    snprintf(path,sizeof(path),"%s/%s",out,names[count]);assert(save_state(path));
                    fprintf(manifest,"%s\t%u\t%06x\t%06x\t%u\t%u\n",names[count],want[count],pc,cursor,key[2],key[3]);
                    count++;
                }
            }
            if(pc==0x28f66 && a6==0x3204e)committed=1;
            m68k_execute(1);assert(!g_stop);
        }
        for(unsigned i=0;i<count;i++) {
            /* First complete another animation to expose stale host state. */
            fresh(input,!enabled);native_finish();
            snprintf(path,sizeof(path),"%s/%s",out,names[i]);assert(load_state(path));
            g_stop=0;g_dragon_fire_fix=g_retail_parity=1;native_finish();
            unsigned final[21];state_regs(final);
            assert(!memcmp(expected[want[i]],g_ram,RAM_SIZE));
            assert(!memcmp(regs[want[i]],final,sizeof(final)));
        }
        total+=count;printf("PASS:%u %s instruction-boundary warm loads\n",count,enabled?"new":"legacy");
    }
    fclose(manifest);
    for(unsigned i=0;i<2;i++) {
        snprintf(path,sizeof(path),"%s/expected-%u.ram",out,i);FILE *f=fopen(path,"wb");assert(f);
        assert(fwrite(expected[i],1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
    printf("PASS:%u total warm boundaries; pending legacy loops preserve their native state\n",total);
    free(expected[0]);free(expected[1]);free(scratch);
}

int main(int argc,char **argv) {
    int n=1,log=0;const char *tests=NULL,*out=NULL,*resume=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--animation-oracle"))oracle=1;
        else if(!strcmp(argv[i],"--animation-limit")&&i+1<argc)limit=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--animation-capture")&&i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--animation-trace"))tracing=1;
        else if(!strcmp(argv[i],"--fire-tests")&&i+1<argc)tests=argv[++i];
        else if(!strcmp(argv[i],"--fire-output")&&i+1<argc)out=argv[++i];
        else if(!strcmp(argv[i],"--fire-resume")&&i+1<argc)resume=argv[++i];
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=1;argv[n++]=argv[i];}
    }
    if(!log){fprintf(stderr,"Scratch --log required.\n");return 2;}
    argv[n]=NULL;
    int rc=moonstone_main(n,argv);if(rc)return rc;
    g_log=NULL;oracle=tracing=0;record=0;g_stop=0;
    if(tests){assert(out);unit_checks(tests,out);}
    if(resume) {
        native_finish();FILE *f=fopen(resume,"wb");assert(f);
        assert(fwrite(g_ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
    return 0;
}
