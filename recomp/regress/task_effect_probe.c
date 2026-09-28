/* Test-only native retail oracle, interrupt, save and renderer regressions.
 * Never deploy this executable; every invocation requires a scratch --log. */
#define moon_instr_hook task_original_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>

#define STOP 0x1ef000u
#define STACK 0x1ff000u
#define STUB1 0x1ee000u
#define STUB2 0x1ee010u
static int original_only,trace_tasks;
static unsigned starts,overlaps,cursors,dispatches,holes,max_entries,duplicates,shake_cursor_overlap;
static const char *snapshot_dir;
static unsigned irq_at,irq_a0,irq_seen,effect_seen,cursor_seen;
static int irq_mode;
static int irq_line=-1;

static unsigned list(int ref){return ref?0x3bd7e:0x3c096;}
static unsigned effect(int ref){return ref?0x29fcc:0x2a10c;}
static unsigned cursor(int ref){return ref?0x2d3ea:0x2d61c;}
static unsigned start_effect(int ref){return ref?0x29f6c:0x2a0b8;}
static unsigned count(unsigned address,unsigned value){unsigned n=0;for(unsigned i=0;i<9;i++)n+=r32(address+i*4)==value;return n;}
static void event(const char *kind,unsigned pc) {
    if(!g_log)return;
    fprintf(g_log,"TASK-TRACE %s pc=%06x fr=%d beam=%u cyc=%u sr=%04x caller=%06x active=%u timer=%u pos=%u cursor=%u list=",
        kind,pc,g_cur_frame,g_beam_line,g_frame_cycle,m68k_get_reg(NULL,M68K_REG_SR),
        r32(m68k_get_reg(NULL,M68K_REG_A7)),count(list(0),effect(0)),r16(0x2a158),
        r32(0x2a154)-0x2a162,count(list(0),cursor(0)));
    for(unsigned i=0;i<9;i++)fprintf(g_log,"%s%06x",i?",":"",r32(list(0)+i*4));
    fputc('\n',g_log);
}
void moon_instr_hook(unsigned pc) {
    if(irq_mode) {
        if(pc==0x3bb4e||pc==0x3b836)irq_seen++;
        if(pc==effect(g_lineage==LIN_RETAIL))effect_seen++;
        if(pc==cursor(g_lineage==LIN_RETAIL))cursor_seen++;
    }
    if(trace_tasks && g_os && g_lineage==LIN_CRACKED) {
        if(pc==0x2a0b8 && r16(pc)==0x7002) {
            starts++;if(count(list(0),effect(0)))overlaps++;event("shake-start",pc);
            if(snapshot_dir && starts<=4) {
                char path[1024];snprintf(path,sizeof(path),"%s/shake-%u.sav",snapshot_dir,starts);
                assert(save_state(path));
            }
        }
        if(pc==0x2a0f2 && r16(pc)==0x33fc)event("shake-end",pc);
        if(pc==0x2d586 && r16(pc)==0x4a79) {cursors++;event("cursor-start",pc);}
        if(pc==0x2d5de && r16(pc)==0x217c)event("cursor-append",pc);
        if(pc==0x2d618 && r16(pc)==0x4290)event("cursor-remove",pc);
        if(pc==0x3b906 && r16(pc)==0x48e7) {
            dispatches++;unsigned entries=0,first_null=9,e=count(list(0),effect(0)),c=count(list(0),cursor(0));
            for(unsigned i=0;i<9;i++) {if(r32(list(0)+i*4))entries++;else if(first_null==9)first_null=i;}
            if(entries>max_entries)max_entries=entries;
            if(entries>first_null) {holes++;if(holes<5)event("HOLE",pc);}
            if(e>1||c>1){duplicates++;if(duplicates<5)event("DUPLICATE",pc);}
            if(e&&c)shake_cursor_overlap++;
        }
    }
    if(!original_only)task_original_hook(pc);
}
static unsigned execute_until(unsigned stop) {
    unsigned n=0,elapsed=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=stop && n++<500000) {
        if(irq_at && m68k_get_reg(NULL,M68K_REG_PC)==irq_at &&
           (!irq_a0||m68k_get_reg(NULL,M68K_REG_A0)==irq_a0)&&
           (irq_line<0||g_beam_line==(unsigned)irq_line)) {
            irq_at=0;g_custom[0x09a>>1]=0x4020;g_custom[0x09c>>1]=0x0020;update_ipl();
        }
        int did=m68k_execute(1);assert(did>0);elapsed+=(unsigned)did;
        g_frame_cycle=(g_frame_cycle+(unsigned)did)%CYCLES_PER_FRAME;beam_update();
    }
    if(m68k_get_reg(NULL,M68K_REG_PC)!=stop)fprintf(stderr,"stop=%x actual=%x\n",stop,m68k_get_reg(NULL,M68K_REG_PC));
    assert(m68k_get_reg(NULL,M68K_REG_PC)==stop);return elapsed;
}
static unsigned call(unsigned pc) {
    m68k_set_reg(M68K_REG_A7,STACK);w32(STACK,STOP);m68k_set_reg(M68K_REG_PC,pc);
    return execute_until(STOP);
}
static void setup(const uint8_t *ram,int ref,int raw) {
    memcpy(g_ram,ram,RAM_SIZE);original_only=raw;g_os=!raw;g_lineage=ref?LIN_RETAIL:LIN_CRACKED;
    g_retail_parity=!raw;g_tasklist_fix=1;g_frame_cycle=10*CYCLES_PER_LINE;beam_update();
    memset(g_ram+list(ref),0,9*4);w32(list(ref),STUB1);w32(list(ref)+4,STUB2);
    w16(STUB1,0x4e75);w16(STUB2,0x4e75);w16(STOP,0x60fe);
    g_custom[0x08e>>1]=0x2c81;g_custom[0x090>>1]=0xf4c1;
    w16(ref?0x39088:0x392c8,0);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
}
static void cursor_stubs(int ref) {
    /* Isolate list registration from sprite allocation/drawing. No scheduler/effect stubs. */
    w16(ref?0x41262:0x41636,0x4e75);w16(ref?0x4128c:0x41660,0x4e75);
    w16(ref?0x41292:0x41666,0x4e75);w16(ref?0x4127e:0x41652,0x4e75);
}
static uint8_t *readram(const char *dir,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,104,SEEK_SET));
    uint8_t *ram=malloc(RAM_SIZE);assert(ram&&fread(ram,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);return ram;
}
static void renderer_tests(const uint8_t *ram) {
    setup(ram,0,0);call(start_effect(0));g_recover=0;
    memset(g_custom,0,sizeof(g_custom));
    /* Synthetic packed bitplanes: asymmetric rows, a nonblack border, and a
     * fixed hardware sprite. Check every RGB pixel, including clipped rows. */
    unsigned table=0x100000,base=0x101000,sprite=0x120000;
    memset(g_ram+table,0,128);memset(g_ram+base,0,5*8000);
    w16(table+48,0xffff);w16(table+50,0xfffe);
    for(unsigned p=0;p<5;p++) {
        w16(table+p*8+2,(base+p*8000)>>16);w16(table+p*8+6,(base+p*8000)&65535);
        for(unsigned y=0;y<200;y++)for(unsigned x=0;x<40;x++)
            w8(base+p*8000+y*40+x,(uint8_t)(y*7+x*13+p*23));
    }
    g_custom[0x080>>1]=table>>16;g_custom[0x082>>1]=table&65535;
    g_custom[0x08e>>1]=0x2c81;g_custom[0x090>>1]=0xf4c1;
    g_custom[0x100>>1]=0x5000;
    for(unsigned p=0;p<32;p++)g_custom[(0x180+p*2)>>1]=(p*117+0x123)&0xfff;
    size_t bytes=FB_W*FB_H*3u;
    uint8_t *ref=calloc(1,bytes),*got=calloc(1,bytes),*expected=calloc(1,bytes),*canary=malloc(bytes+64);
    assert(ref&&got&&expected&&canary);
    int w,h;render_rgb(ref,&w,&h);assert(w==320&&h==200);
    const int offsets[]={0,8,-8,2,-2,1,-1};
    for(unsigned spr=0;spr<2;spr++)for(unsigned i=0;i<7;i++) {
        int dy=offsets[i];
        g_custom[0x08e>>1]=(uint16_t)(0x2c81+dy*256);
        g_custom[0x090>>1]=(uint16_t)(0xf4c1+dy*256);
        g_custom[0x120>>1]=spr?sprite>>16:0;g_custom[0x122>>1]=spr?sprite&65535:0;
        memset(g_ram+sprite,0,24);w16(sprite,0x664a);w16(sprite+2,0x6a00);
        for(unsigned row=0;row<4;row++)w16(sprite+4+row*4,0xffff);
        memset(expected,0,bytes);
        for(int y=0;y<200;y++)for(int x=0;x<320;x++) {
            unsigned char *pixel=expected+(y*FB_W+x)*3;
            if(y-dy>=0&&y-dy<200)memcpy(pixel,ref+((y-dy)*FB_W+x)*3,3);
            else {pixel[0]=0x11;pixel[1]=0x22;pixel[2]=0x33;}
            if(spr&&y>=58&&y<62&&x>=20&&x<36) {
                unsigned color=g_custom[0x1a2>>1];
                pixel[0]=((color>>8)&15)*17;pixel[1]=((color>>4)&15)*17;pixel[2]=(color&15)*17;
            }
        }
        memset(canary,0xa5,bytes+64);memset(canary+32,0,bytes);
        render_rgb(canary+32,&w,&h);assert(w==320&&h==200);
        memcpy(got,canary+32,bytes);
        if(memcmp(got,expected,200*FB_W*3)) {
            for(unsigned p=0;p<200*FB_W*3;p++)if(got[p]!=expected[p]) {
                printf("PIXEL mismatch sprite=%u dy=%d row=%u x=%u channel=%u got=%u expected=%u active=%x guard=%d parity=%d\n",spr,dy,p/(FB_W*3),(p/3)%FB_W,p%3,got[p],expected[p],tasklist_find(effect(0)),task_effect_code(),g_retail_parity);break;
            }
        }
        assert(!memcmp(got,expected,200*FB_W*3));
        for(unsigned n=0;n<32;n++)assert(canary[n]==0xa5&&canary[bytes+32+n]==0xa5);
    }
    puts("PASS: 14 pixel-exact renders, original offsets, COLOR00 borders, clipping, fixed hardware cursor and buffer bounds");
    g_custom[0x120>>1]=g_custom[0x122>>1]=0;
    g_custom[0x08e>>1]=0x3481;g_custom[0x090>>1]=0xfcc1;
    for(unsigned gate=0;gate<4;gate++) {
        g_retail_parity=gate!=0;g_os=1;g_lineage=gate==1?LIN_RETAIL:LIN_CRACKED;
        w16(0x2a0b8,gate==2?0x4e71:0x7002);w32(list(0)+8,gate==3?0:effect(0));
        memset(got,0,bytes);render_rgb(got,&w,&h);assert(!memcmp(got,ref,200*FB_W*3));
    }
    puts("PASS: effect rendering is inert with parity off, foreign lineage, overlaid code or no active effect");
    free(ref);free(got);free(expected);free(canary);
}
static void tests(const char *dir,const char *out) {
    uint8_t *ram[2]={readram(dir,"current.sav"),readram(dir,"retail-runtime.sav")};
    uint16_t values[2][36][2];
    for(int ref=0;ref<2;ref++) {
        setup(ram[ref],ref,ref);call(start_effect(ref));assert(count(list(ref),effect(ref))==1);
        for(unsigned tick=0;tick<36;tick++) {
            call(effect(ref));values[ref][tick][0]=g_custom[0x08e>>1];values[ref][tick][1]=g_custom[0x090>>1];
            assert(count(list(ref),effect(ref))==(tick<35));
        }
        assert(r32(list(ref))==STUB1&&r32(list(ref)+4)==STUB2&&r32(list(ref)+8)==0);
    }
    assert(!memcmp(values[0],values[1],sizeof(values[0])));
    puts("PASS: both native effects complete in 36 ticks with identical screen-position writes and preserved resident callbacks");

    for(unsigned gap=0;gap<36;gap++)for(int ref=0;ref<2;ref++) {
        setup(ram[ref],ref,ref);call(start_effect(ref));for(unsigned i=0;i<gap;i++)call(effect(ref));
        unsigned prior_timer=r16(ref?0x2a014:0x2a158),prior_pos=r32(ref?0x2a010:0x2a154);
        call(start_effect(ref));assert(count(list(ref),effect(ref))==1);
        assert(r16(ref?0x2a014:0x2a158)==prior_timer);
        assert(r32(ref?0x2a010:0x2a154)==prior_pos);
        unsigned remaining=0;while(count(list(ref),effect(ref))&&remaining++<100)call(effect(ref));
        assert(remaining==36-gap);
    }
    puts("PASS: 72 overlapping-start cases: port and original retail keep the first impact's 36-tick deadline");

    setup(ram[0],0,1);call(start_effect(0));call(start_effect(0));assert(count(list(0),effect(0))==2);
    puts("PASS: unprotected original-port control appends two copies; current and retail retain only one");

    unsigned min=0xffffffffu,max=0;
    for(unsigned line=0;line<313;line++) {
        setup(ram[1],1,1);g_frame_cycle=line*CYCLES_PER_LINE;beam_update();
        unsigned cycles=call(0x3fafa);assert(g_beam_line==246);
        if(cycles<min)min=cycles;if(cycles>max)max=cycles;
    }
    printf("PASS: 313 actual retail raster waits exit at line246; elapsed %u..%u emulated cycles\n",min,max);

    /* Same installed-flag gate, then list-based safeguards in current. */
    for(int ref=0;ref<2;ref++)for(unsigned repeat=1;repeat<=12;repeat++) {
        setup(ram[ref],ref,ref);cursor_stubs(ref);
        for(unsigned n=0;n<repeat;n++)call(ref?0x2d34e:0x2d586);
        assert(count(list(ref),cursor(ref))==1);call(ref?0x2d3c2:0x2d5f4);
        assert(count(list(ref),cursor(ref))==0&&r32(list(ref))==STUB1&&r32(list(ref)+4)==STUB2);
    }
    puts("PASS: 24 repeated cursor installation/removal sequences stay singular and preserve resident tasks");

    /* Reproduce the historical shifted-slot layout; current value-removal is already fixed. */
    for(unsigned order=0;order<2;order++) {
        setup(ram[0],0,0);cursor_stubs(0);
        if(order){call(0x2d586);call(0x2a0b8);}else{call(0x2a0b8);call(0x2d586);}
        for(unsigned n=0;n<36;n++)call(0x2a10c);
        assert(count(list(0),cursor(0))==1 && r32(list(0)+8)==cursor(0));
        call(0x2d5f4);assert(count(list(0),cursor(0))==0&&r32(list(0)+8)==0);
    }
    puts("PASS: current protects both effect/cursor orderings and stale saved cursor slots");

    /* Controlled instruction-boundary interrupt: run the real native L3 ISR,
     * dispatcher, shake terminator, acknowledgement and RTE, not a model. */
    const unsigned points[]={0x2a0c4,0x2a0ca,0x2a0d0,0x2a0d8,0x2d5de};
    for(unsigned i=0;i<sizeof(points)/sizeof(points[0]);i++) {
        setup(ram[0],0,0);cursor_stubs(0);call(start_effect(0));
        for(unsigned tick=0;tick<35;tick++)call(effect(0));
        assert(r16(0x2a158)==1&&r32(0x2a154)==0x2a178);
        w32(0x6c,0x3bb4e);w16(0x3c0ba,1);w16(0x3c10a,1);
        irq_at=points[i];irq_a0=0;irq_seen=effect_seen=cursor_seen=0;irq_mode=1;
        /* Enter past the early guard to cover saves made by the previous build
         * in its installer, as well as the ordinary cursor race. */
        m68k_set_reg(M68K_REG_SR,0x2000);call(i==4?0x2d586:0x2a0ba);
        assert(!irq_at&&irq_seen==1&&effect_seen==1);
        assert(!(g_custom[0x09c>>1]&0x20));
        unsigned wanted=i==4?cursor(0):effect(0);
        if(r32(list(0)+8)!=wanted||r32(list(0)+12)!=0)
            printf("IRQ mismatch point=%x slots=%x,%x,%x,%x guard=%d lineage=%d os=%d a0=%x saved=%x\n",points[i],r32(list(0)),r32(list(0)+4),r32(list(0)+8),r32(list(0)+12),task_effect_code(),g_lineage,g_os,m68k_get_reg(NULL,M68K_REG_A0),r32(0x2a15e));
        assert(r32(list(0)+8)==wanted&&r32(list(0)+12)==0);
        if(i==4)w16(cursor(0),0x4e75); /* cursor body checked separately below */
        unsigned prior=effect_seen+cursor_seen;call(0x3b906);
        assert(effect_seen+cursor_seen==prior+1);
        printf("PASS: controlled native IRQ at %06x: no hole and the registered callback is dispatched\n",points[i]);
        irq_mode=0;m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
    }
    /* Retail's early duplicate guard avoids this overlapping-shake window:
     * inject just before or just after comparing the existing effect entry. */
    for(unsigned after_compare=0;after_compare<2;after_compare++) {
        setup(ram[1],1,1);call(start_effect(1));
        for(unsigned tick=0;tick<35;tick++)call(effect(1));
        w32(0x6c,0x3b836);w16(0x3bda2,1);w16(0x3be02,1);
        irq_at=after_compare?0x29f78:0x29f72;irq_a0=list(1)+8;
        irq_seen=effect_seen=cursor_seen=0;irq_mode=1;
        m68k_set_reg(M68K_REG_SR,0x2000);call(start_effect(1));
        assert(!irq_at&&irq_seen==1&&effect_seen==1);
        assert(r32(list(1)+8)==(after_compare?0:effect(1))&&r32(list(1)+12)==0);
        irq_mode=0;irq_a0=0;m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
    }
    puts("PASS: retail early duplicate guard leaves no hole in either corresponding shake-overlap interrupt ordering");

    for(unsigned after_check=0;after_check<2;after_check++) {
        setup(ram[0],0,0);call(start_effect(0));
        for(unsigned tick=0;tick<35;tick++)call(effect(0));
        w32(0x6c,0x3bb4e);w16(0x3c0ba,1);w16(0x3c10a,1);
        /* The host check and guest RTS execute as one instruction step;
         * the next interrupt boundary is the returned-to caller. */
        irq_at=after_check?STUB1:0x2a0b8;irq_a0=0;
        irq_seen=effect_seen=cursor_seen=0;irq_mode=1;
        m68k_set_reg(M68K_REG_SR,0x2000);call(start_effect(0));
        if(after_check)call(STUB1);
        assert(!irq_at&&irq_seen==1&&effect_seen==1);
        assert(r32(list(0)+8)==(after_check?0:effect(0))&&r32(list(0)+12)==0);
        irq_mode=0;m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
    }
    puts("PASS: new early overlap check is safe with an actual native interrupt on either side");

    setup(ram[1],1,1);cursor_stubs(1);call(start_effect(1));
    for(unsigned tick=0;tick<35;tick++)call(effect(1));
    w32(0x6c,0x3b836);w16(0x3bda2,1);w16(0x3be02,1);
    g_frame_cycle=300*CYCLES_PER_LINE;beam_update();
    irq_at=0x3fafa;irq_a0=0;irq_line=0;irq_seen=effect_seen=cursor_seen=0;irq_mode=1;
    m68k_set_reg(M68K_REG_SR,0x2000);call(0x2d34e);
    assert(!irq_at&&irq_seen==1&&effect_seen==1&&g_beam_line==246);
    assert(r32(list(1)+8)==0&&r32(list(1)+12)==cursor(1));
    call(0x3b5ee);assert(!cursor_seen);
    irq_mode=0;irq_line=-1;m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
    puts("PASS: retail cursor raster wait also leaves a hole if the old shake completes at line0 during that wait; copying the wait alone is insufficient");

    /* Actual cursor callbacks through their dispatchers: original movement/input
     * and sprite-position code runs; one sprite buffer is seeded locally. */
    unsigned cursor_cases=0;
    for(unsigned input=0;input<32;input++)for(unsigned edge=0;edge<3;edge++) {
        unsigned outputs[2][5];
        for(int ref=0;ref<2;ref++) {
            setup(ram[ref],ref,ref);
            unsigned reader=ref?0x22f92:0x22fe6,actor=0x100000;
            /* Input-only stub returns both original input words, preserving A6. */
            w16(reader,0x303c);w16(reader+2,input);w16(reader+4,0x323c);w16(reader+6,input);w16(reader+8,0x4e75);
            w32(ref?0x2f8b6:0x2fb08,actor);memset(g_ram+actor,0,0x84);
            w32(ref?0x413b8:0x4178c,0x100200);memset(g_ram+0x100200,0,64);w16(0x100200,8);
            w16(ref?0x39092:0x392d4,edge==0?100:edge==1?0:314);
            w16(ref?0x39094:0x392d6,edge==0?100:edge==1?0:195);
            w32(ref?0x38f68:0x391a8,0x12345678);
            w32(list(ref),cursor(ref));w32(list(ref)+4,0);
            for(unsigned reg=0;reg<15;reg++)m68k_set_reg((m68k_register_t)reg,0x200000+reg*0x101);
            call(ref?0x3b5ee:0x3b906);
            for(unsigned reg=0;reg<15;reg++)assert(m68k_get_reg(NULL,(m68k_register_t)reg)==0x200000+reg*0x101);
            outputs[ref][0]=r16(ref?0x39092:0x392d4);outputs[ref][1]=r16(ref?0x39094:0x392d6);
            outputs[ref][2]=r16(ref?0x39096:0x392d8);outputs[ref][3]=r32(0x100202);
            outputs[ref][4]=r32(ref?0x38f68:0x391a8);
            assert(outputs[ref][4]!=0x12345678);cursor_cases++;
        }
        assert(!memcmp(outputs[0],outputs[1],sizeof(outputs[0])));
    }
    printf("PASS: %u current/retail cursor ticks: input, limits, sprite coordinates, caller registers and RNG match\n",cursor_cases);

    for(unsigned n=0;n<=9;n++)for(unsigned cur=0;cur<2;cur++) {
        setup(ram[0],0,0);cursor_stubs(0);memset(g_ram+list(0),0,36);
        for(unsigned i=0;i<n;i++)w32(list(0)+i*4,STUB1);
        w32(0x3c0ba,0xdeadbeef);call(cur?0x2d586:start_effect(0));
        assert(r32(0x3c0ba)==0xdeadbeef);
        if(n<8){assert(r32(list(0)+4*n)==(cur?cursor(0):effect(0)));assert(r32(list(0)+4*(n+1))==0);}
        else assert(!count(list(0),cur?cursor(0):effect(0)));
    }
    puts("PASS: 20 registration capacity cases preserve a terminator and adjacent native flags");

    const unsigned hook_sites[]={0x2a0b8,0x2a0ea,0x2d586,0x2d5de};
    uint8_t *untouched=malloc(RAM_SIZE);assert(untouched);
    for(unsigned site=0;site<4;site++)for(unsigned gate=0;gate<4;gate++) {
        setup(ram[0],0,0);w32(list(0)+8,effect(0));w32(list(0)+12,cursor(0));
        g_os=gate!=0;g_tasklist_fix=gate!=1;g_lineage=gate==2?LIN_RETAIL:LIN_CRACKED;
        if(gate==3)w32(site<2?0x2a0ec:0x2d5e0,0x1abcde); /* wrong callback operand */
        m68k_set_reg(M68K_REG_A0,0x123400);m68k_set_reg(M68K_REG_PC,hook_sites[site]);
        memcpy(untouched,g_ram,RAM_SIZE);task_original_hook(hook_sites[site]);
        assert(!memcmp(untouched,g_ram,RAM_SIZE));
        assert(m68k_get_reg(NULL,M68K_REG_A0)==0x123400&&m68k_get_reg(NULL,M68K_REG_PC)==hook_sites[site]);
    }
    free(untouched);puts("PASS: 16 foreign/disabled/overlaid registration hooks are inert");
    setup(ram[0],0,0);g_retail_parity=0;call(start_effect(0));call(effect(0));call(start_effect(0));
    assert(count(list(0),effect(0))==1&&r16(0x2a158)==3&&r32(0x2a154)==0x2a162);
    setup(ram[0],0,0);g_tasklist_fix=0;call(start_effect(0));call(start_effect(0));
    assert(count(list(0),effect(0))==2);
    puts("PASS: parity-off retains the previous restart policy; notaskfix retains the unprotected control");

    for(unsigned ccr=0;ccr<32;ccr++) {
        unsigned results[2][16];
        for(unsigned ref=0;ref<2;ref++) {
            setup(ram[ref],ref,ref);call(start_effect(ref));call(effect(ref));
            for(unsigned r=0;r<15;r++)m68k_set_reg((m68k_register_t)r,0x160000+r*0x101);
            m68k_set_reg(M68K_REG_SR,0x2700|ccr);call(start_effect(ref));
            for(unsigned r=0;r<15;r++)results[ref][r]=m68k_get_reg(NULL,(m68k_register_t)r);
            results[ref][8]-=list(ref);results[ref][15]=m68k_get_reg(NULL,M68K_REG_SR);
        }
        assert(!memcmp(results[0],results[1],sizeof(results[0])));
    }
    puts("PASS: 32 overlap caller-register/CCR cases match original retail");

    char saved[1024];snprintf(saved,sizeof(saved),"%s/native-boundary.sav",out);
    uint8_t *finished=malloc(RAM_SIZE);assert(finished);
    for(unsigned tick=0;tick<36;tick++) {
        setup(ram[0],0,0);call(start_effect(0));
        for(unsigned t=0;t<tick;t++)call(effect(0));
        assert(save_state(saved));
        for(unsigned t=tick;t<36;t++)call(effect(0));
        memcpy(finished,g_ram,RAM_SIZE);
        assert(load_state(saved));call(start_effect(0)); /* re-trigger must not restart */
        for(unsigned t=tick;t<36;t++)call(effect(0));
        assert(!memcmp(finished,g_ram,RAM_SIZE));
        assert(g_custom[0x08e>>1]==0x2c81&&g_custom[0x090>>1]==0xf4c1);
    }
    free(finished);puts("PASS: 36 mid-effect warm restores finish at the same native deadline, with identical RAM");
    renderer_tests(ram[0]);
    free(ram[0]);free(ram[1]);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);const char *dir=NULL,*out=NULL;int n=1,log=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--task-tests")&&i+1<argc)dir=argv[++i];
        else if(!strcmp(argv[i],"--task-output")&&i+1<argc)out=argv[++i];
        else if(!strcmp(argv[i],"--task-trace"))trace_tasks=1;
        else if(!strcmp(argv[i],"--task-snapshots")&&i+1<argc)snapshot_dir=argv[++i];
        else {if(!strcmp(argv[i],"--log")&&i+1<argc)log=1;argv[n++]=argv[i];}
    }
    if(!log){fprintf(stderr,"Explicit scratch --log required\n");return 2;}
    argv[n]=NULL;int rc=moonstone_main(n,argv);if(rc)return rc;
    if(trace_tasks)printf("TASK-SUMMARY starts=%u overlaps=%u cursor_installs=%u dispatches=%u holes=%u duplicates=%u max_entries=%u shake_cursor_ticks=%u\n",
        starts,overlaps,cursors,dispatches,holes,duplicates,max_entries,shake_cursor_overlap);
    if(dir){assert(out);g_log=NULL;trace_tasks=0;snapshot_dir=NULL;tests(dir,out);}
    return 0;
}
