/* Native cursor/RNG oracle and interrupted-call regressions. Never deploy.
 * All invocations require an explicit scratch --log. */
#define moon_instr_hook cursor_production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

#define STOP 0x1ef000u
#define STACK 0x1ff000u
static int trace_rng;
static unsigned cursor_ticks, added_calls, native_calls, interrupt_entries;
void moon_instr_hook(unsigned pc) {
    if(pc==0x3bb4e || pc==0x3b836)interrupt_entries++;
    if(trace_rng && g_lineage==LIN_CRACKED) {
        if(pc==0x2b2ca && r16(pc)==0x23c0) {
            native_calls++;
            if(g_log)fprintf(g_log,"RNG-DRAW fr=%d caller=%x before=%08x after=%08x\n",
                g_cur_frame,r32(m68k_get_reg(NULL,M68K_REG_A7)+8),r32(0x391a8),
                m68k_get_reg(NULL,M68K_REG_D0));
        }
        if(pc==0x2d6de && r16(pc)==0x4e75)cursor_ticks++;
    }
    cursor_production_hook(pc);
    if(trace_rng && pc==0x2d6de && m68k_get_reg(NULL,M68K_REG_PC)==0x2b2b0)added_calls++;
}
static void until(unsigned pc) {
    unsigned steps=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=pc) {
        assert(++steps<4096);m68k_execute(1);assert(!g_stop);
    }
}
static uint8_t *readram(const char *dir,const char *name) {
    char path[1200];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,104,SEEK_SET));
    uint8_t *ram=malloc(RAM_SIZE);assert(ram&&fread(ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);return ram;
}
static void prepare(const uint8_t *ram,unsigned ref,unsigned seed,unsigned ccr,unsigned input,unsigned edge) {
    memcpy(g_ram,ram,RAM_SIZE);g_os=!ref;g_lineage=ref?LIN_RETAIL:LIN_CRACKED;
    g_retail_parity=g_cursor_rng_fix=1;g_stop=0;m68k_set_irq(0);
    m68k_set_reg(M68K_REG_SR,0x2700|ccr);
    for(unsigned r=0;r<15;r++)m68k_set_reg((m68k_register_t)r,0x200000+r*0x101);
    unsigned reader=ref?0x22f92:0x22fe6,actor=0x100000;
    /* Stub only external input. Original cursor, sprite-position function,
     * dispatcher and RNG all execute their own instructions. */
    w16(reader,0x303c);w16(reader+2,input);w16(reader+4,0x323c);
    w16(reader+6,input^15);w16(reader+8,0x4e75);
    w32(ref?0x2f8b6:0x2fb08,actor);memset(g_ram+actor,0,0x84);w8(actor+11,(seed>>2)&1);
    w32(ref?0x413b8:0x4178c,0x100200);memset(g_ram+0x100200,0,64);w16(0x100200,8);
    w16(ref?0x39092:0x392d4,edge==0?100:edge==1?0:314);
    w16(ref?0x39094:0x392d6,edge==0?100:edge==1?0:195);
    w32(ref?0x38f68:0x391a8,seed);
    unsigned list=ref?0x3bd7e:0x3c096;memset(g_ram+list,0,36);
    w32(list,ref?0x2d3ea:0x2d61c);
    w16(STOP,0x60fe);m68k_set_reg(M68K_REG_A7,STACK);w32(STACK,STOP);
    m68k_set_reg(M68K_REG_PC,ref?0x3b5ee:0x3b906);
}
static void registers(void) {
    for(unsigned r=0;r<15;r++)assert(m68k_get_reg(NULL,(m68k_register_t)r)==0x200000+r*0x101);
    assert(m68k_get_reg(NULL,M68K_REG_A7)==STACK+4);
}
static void result(unsigned ref,unsigned *out) {
    registers();out[0]=r32(ref?0x38f68:0x391a8);
    out[1]=r32(ref?0x39092:0x392d4);out[2]=r16(ref?0x39096:0x392d8);
    out[3]=r32(0x100202);out[4]=m68k_get_reg(NULL,M68K_REG_SR);
}
static void finish(const char *path) {
    until(STOP);registers();
    if(path) {
        FILE *f=fopen(path,"wb");assert(f);assert(fwrite(g_ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
    }
}
static void tests(const char *dir,const char *out) {
    uint8_t *ram[2]={readram(dir,"current.sav"),readram(dir,"retail-runtime.sav")};
    unsigned cases=0;
    for(unsigned index=0;index<64;index++)for(unsigned flags=0;flags<32;flags++) {
        unsigned seed=index==0?0:index==1?~0u:index==2?0xfffffde2u:index==3?0xc88fu:
                      index==4?0xfffffffbu:index==5?0xacfb:index*0x9e3779b9u;
        unsigned outputs[2][10];
        for(unsigned ref=0;ref<2;ref++) {
            prepare(ram[ref],ref,seed,flags,index&31,index%3);
            until(STOP);result(ref,outputs[ref]);
            m68k_set_reg(M68K_REG_A7,STACK);m68k_set_reg(M68K_REG_PC,ref?0x3b5ee:0x3b906);
            until(STOP);result(ref,outputs[ref]+5);
        }
        assert(!memcmp(outputs[0],outputs[1],sizeof(outputs[0])));cases++;
    }
    printf("PASS: %u retail/current pairs, two consecutive ticks; seeds, all CCR bits, both input ports, cursor limits, sprite coordinates and all caller registers match\n",cases);

    /* The foreground can be inside this same RNG when VBlank updates the
     * cursor. Execute both original interrupt handlers at every RNG boundary;
     * retail's own interrupted-stream behavior is the oracle. */
    for(unsigned boundary=0;boundary<62;boundary++) {
        unsigned state[2][18];
        for(unsigned ref=0;ref<2;ref++) {
            prepare(ram[ref],ref,0x12345678,16,0,0);
            w32(0x6c,ref?0x3b836:0x3bb4e);
            w16(ref?0x3bda2:0x3c0ba,1);w16(ref?0x3be02:0x3c10a,1);
            g_custom[0x09a>>1]=0;g_custom[0x09c>>1]=0;update_ipl();
            g_frame_cycle=10*CYCLES_PER_LINE;beam_update();
            m68k_set_reg(M68K_REG_SR,0x2010);
            m68k_set_reg(M68K_REG_PC,ref?0x2b188:0x2b2b0);
            for(unsigned n=0;n<boundary;n++){assert(m68k_get_reg(NULL,M68K_REG_PC)!=STOP);m68k_execute(1);}
            interrupt_entries=0;g_custom[0x09a>>1]=0x4020;g_custom[0x09c>>1]=0x0020;update_ipl();
            until(STOP);assert(interrupt_entries==1);
            for(unsigned r=0;r<16;r++)state[ref][r]=m68k_get_reg(NULL,(m68k_register_t)r);
            state[ref][16]=m68k_get_reg(NULL,M68K_REG_SR);state[ref][17]=r32(ref?0x38f68:0x391a8);
        }
        assert(!memcmp(state[0],state[1],sizeof(state[0])));
    }
    puts("PASS: 62 foreground RNG boundaries interrupted by actual native VBlank; retail/current RNG, registers, stack and flags match");

    uint8_t *untouched=malloc(RAM_SIZE);assert(untouched);
    for(unsigned mode=0;mode<56;mode++) {
        prepare(ram[0],0,0x12345678,31,0,0);until(0x2d6de);
        unsigned sp=m68k_get_reg(NULL,M68K_REG_A7);
        if(mode==0)g_os=0;if(mode==1)g_retail_parity=0;if(mode==2)g_cursor_rng_fix=0;
        if(mode==3)g_lineage=LIN_RETAIL;if(mode==4)g_lineage=LIN_UNKNOWN;
        if(mode==5)w16(0x2d61c,0x4e71);if(mode==6)w32(0x2d620,0x392d6);
        if(mode==7)w16(0x2d6d8,0x4e71);if(mode==8)w32(0x2d6da,0x41664);
        if(mode==9)w16(0x2d6de,0x4e71);if(mode==10)w16(0x3b916,0x4e71);
        if(mode==11)w16(0x3b91a,0x4e71);if(mode==12)w16(0x3b91c,0x4e71);
        if(mode==13)m68k_set_reg(M68K_REG_A7,sp+1);
        if(mode==14)m68k_set_reg(M68K_REG_A7,6);
        if(mode==15)m68k_set_reg(M68K_REG_A7,RAM_SIZE);
        if(mode==16)w32(sp,STOP);if(mode==17)w32(0x2d5e0,0x2d61a);
        if(mode>=18)w8(0x2b2b0+mode-18,r8(0x2b2b0+mode-18)^1);
        memcpy(untouched,g_ram,RAM_SIZE);unsigned actual_sp=m68k_get_reg(NULL,M68K_REG_A7);
        cursor_rng_hook(0x2d6de);assert(m68k_get_reg(NULL,M68K_REG_PC)==0x2d6de);
        assert(m68k_get_reg(NULL,M68K_REG_A7)==actual_sp&&!memcmp(g_ram,untouched,RAM_SIZE));
    }
    puts("PASS: 56 disabled/foreign/code-signature/stack guards, including every RNG instruction byte");
    prepare(ram[0],0,0x12345678,0,0,0);w32(0x3c096,0);until(STOP);
    assert(r32(0x391a8)==0x12345678);registers();
    puts("PASS: inactive cursor consumes no RNG");

    /* No synthetic host return frame: capture every instruction from the
     * cursor RTS through native RNG and dispatcher, then resume identically. */
    prepare(ram[0],0,0x12345678,31,0,0);until(0x2d6de);
    char path[1200];unsigned boundaries=0;
    do {
        snprintf(path,sizeof(path),"%s/boundary-%02u.sav",out,boundaries++);assert(save_state(path));
        if(m68k_get_reg(NULL,M68K_REG_PC)==STOP)break;
        assert(boundaries<128);m68k_execute(1);
    }while(1);
    snprintf(path,sizeof(path),"%s/expected.ram",out);finish(path);
    memcpy(untouched,g_ram,RAM_SIZE);unsigned expected_sr=m68k_get_reg(NULL,M68K_REG_SR);
    for(unsigned n=0;n<boundaries;n++) {
        /* Unrelated completed callback must not affect the restored stream. */
        prepare(ram[0],0,0xabcde012,0,31,2);until(STOP);
        snprintf(path,sizeof(path),"%s/boundary-%02u.sav",out,n);assert(load_state(path));
        finish(NULL);assert(!memcmp(g_ram,untouched,RAM_SIZE));
        assert(m68k_get_reg(NULL,M68K_REG_SR)==expected_sr);
    }
    printf("PASS: %u instruction-boundary warm loads match uninterrupted full RAM, registers and flags\n",boundaries);
    /* Already-started native calls can unwind with the option disabled. Only
     * boundary0 precedes the new call and correctly remains a no-RNG control. */
    for(unsigned n=1;n<boundaries;n++) {
        snprintf(path,sizeof(path),"%s/boundary-%02u.sav",out,n);assert(load_state(path));
        g_cursor_rng_fix=0;finish(NULL);assert(!memcmp(g_ram,untouched,RAM_SIZE));
    }
    printf("PASS: %u pending native calls finish with cursor RNG disabled\n",boundaries-1);
    prepare(ram[0],0,0x12345678,0,0,0);
    snprintf(path,sizeof(path),"%s/ordinary.sav",out);assert(save_state(path));
    until(0x2b2be);assert(load_state(path));finish(NULL);
    puts("PASS: loading an ordinary save mid-RNG has no stale host continuation");
    free(untouched);free(ram[0]);free(ram[1]);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);const char *dir=NULL,*out=NULL,*resume=NULL;int n=1,log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--rng-tests")&&i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--rng-output")&&i+1<argc)out=argv[++i];
        else if(!strcmp(argv[i],"--rng-resume")&&i+1<argc)resume=argv[++i];
        else if(!strcmp(argv[i],"--rng-trace"))trace_rng=1;
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=1;argv[n++]=argv[i];}
    }
    if(!log){fprintf(stderr,"Explicit scratch --log required\n");return 2;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);if(rc)return rc;
    if(trace_rng)printf("RNG-SUMMARY cursor_ticks=%u added_calls=%u native_calls=%u\n",cursor_ticks,added_calls,native_calls);
    g_log=NULL;trace_rng=0;
    if(dir){assert(out);tests(dir,out);}
    if(resume)finish(resume);
    return 0;
}
