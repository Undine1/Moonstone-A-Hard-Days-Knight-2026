/* Native movement/animation oracle. TEST ONLY: never deploy this executable. */
#define SDL_MAIN_HANDLED
#define moon_instr_hook production_hook
#define main moonstone_main
#include "../src/moon.c"
#undef main
#undef moon_instr_hook
#undef NDEBUG
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"assertion line %d: %s\n",__LINE__,#x); exit(86); } } while(0)
enum { HERO=0x1c0000, STACK=0x1ff000, STOP=0x1ef000 };
static uint8_t base[RAM_SIZE],expected[RAM_SIZE],initial[RAM_SIZE];
static unsigned expected_regs[SAVE_NREGS];
static int edition,fixed;
static int idlebox[10][2][4],anchors[10][2],eligible[10];
static unsigned originals[10][2],normal_mask,mask;
static unsigned pairs,improved,body_cases,scope_cases,solid_cases;
static uint32_t display(void){return edition?0x2ea68:0x2eca0;}
void moon_instr_hook(unsigned pc){
    if(fixed)trogg_movement_hook(pc);
    if(pc==(edition?0x26324u:0x26204u))mask=m68k_get_reg(NULL,M68K_REG_D0)&65535;
}
static void reset(unsigned pc,unsigned stop){
    g_stop=g_unmapped=0;g_os=1;g_lineage=edition?LIN_RETAIL:LIN_CRACKED;
    for(unsigned i=0;i<SAVE_NREGS;i++)m68k_set_reg(SAVE_REGS[i],0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_irq(0);
    m68k_set_reg(M68K_REG_A7,STACK);w32(STACK,stop);m68k_set_reg(M68K_REG_PC,pc);
    g_custom[0x09a>>1]=g_custom[0x09c>>1]=0;
}
static void finish(unsigned stop){
    unsigned steps=0;
    while(m68k_get_reg(NULL,M68K_REG_PC)!=stop){
        if(++steps>8000 || g_stop || g_unmapped){fprintf(stderr,"bad native e%d pc%x steps%u\n",edition,m68k_get_reg(NULL,M68K_REG_PC),steps);abort();}
        m68k_execute(1);
    }
}
static void standing(unsigned slot,unsigned facing,int box[4]){
    memcpy(g_ram,base,RAM_SIZE);fixed=0;
    unsigned d=display()+slot*50,a=r32(d+0x18),table=edition?0x2ea00:0x2ec1c;
    unsigned p=edition?0x285d8:0x285b0,n=0;
    while(r16(p)==0x217c){w32(table+r16(p+6),r32(p+2));p+=8;n++;}
    assert(n==(edition?19u:20u));
    static const unsigned draw[2][4]={{0x28c90,0x28c96,0x28dfa,0x28bb2},{0x28be4,0x28bea,0x28d20,0x28b1e}};
    for(unsigned i=0;i<4;i++){p=draw[edition][i];assert(r16(p)==0x4eb9);w16(r32(p+2),0x4e75);}
    w16(edition?0x3b1c0:0x3b470,0x4e75);
    memset(g_ram+0x1d8000,0,36);w32(d+0x24,0x1d8000);
    w32(d+2,r32(a+0x16));w8(d+1,1);w8(d+0x16,facing);
    w16(d+6,r16(a+4));w16(d+8,r16(a+6));w16(d+10,r16(a+8));
    w32(edition?0x2e9f2:0x2ec0a,0x1d0000);
    w32(edition?0x2e9f6:0x2ec12,0x1d1000);
    w32(edition?0x2e9fa:0x2ec16,0x1d2000);
    w16(r32((edition?0x29526:0x29672)+2),0);
    reset(edition?0x289e8:0x28a7c,STOP);m68k_set_reg(M68K_REG_A1,d);finish(STOP);
    box[0]=(int16_t)r16(a+0x3a);box[1]=(int16_t)r16(a+0x3c);
    box[2]=(int16_t)r16(a+0x70);box[3]=(int16_t)r16(a+0x72);
    if(!(box[1]>box[0] && box[3]>box[2]))fprintf(stderr,"idle e%d a%x d%x script%x face%u box=%d,%d,%d,%d bank%x init%u\n",edition,a,d,r32(d+2),facing,box[0],box[1],box[2],box[3],r32(d+0x1c),n);
    assert(box[1]>box[0] && box[3]>box[2]);
}
static void prepare_boxes(void){
    memset(eligible,0,sizeof(eligible));
    for(unsigned i=0;i<10;i++){
        memcpy(g_ram,base,RAM_SIZE);unsigned d=display()+i*50,a=r32(d+0x18);
        if(!r8(d) || !a || r8(a+0x4d)!=0x1c)continue;
        unsigned script=r32(d+2),start=(edition?0x32c9e:0x32eee)+0x1f4;
        if(script<start || script>=start+0x17e || (int16_t)r16(a+0x50)<=0)continue;
        eligible[i]=1;anchors[i][0]=(int16_t)r16(a+4);anchors[i][1]=(int16_t)r16(a+8);
        standing(i,1,idlebox[i][0]);standing(i,3,idlebox[i][1]);
    }
}
static void run(unsigned mode,unsigned input,unsigned phase,int body,int dx,int face){
    memcpy(g_ram,base,RAM_SIZE);fixed=mode==1;mask=0;
    if(mode==2)for(unsigned i=0;i<10;i++)if(eligible[i]){
        unsigned d=display()+i*50,a=r32(d+0x18);int faceidx=r8(a+0xa)==3;
        originals[i][0]=r16(a+0x3a);originals[i][1]=r16(a+0x3c);
        if((int16_t)originals[i][0]<0 || (int16_t)originals[i][1]<=(int16_t)originals[i][0])continue;
        int shift=(int16_t)r16(a+4)-anchors[i][0];
        int left=idlebox[i][faceidx][0]+shift,right=idlebox[i][faceidx][1]+shift;
        if(left<(int16_t)originals[i][0])left=(int16_t)originals[i][0];
        if(right>(int16_t)originals[i][1])right=(int16_t)originals[i][1];
        if(left<right){w16(a+0x3a,(uint16_t)left);w16(a+0x3c,(uint16_t)right);}
    }
    unsigned entry=body?(edition?0x2631e:0x261fe):(edition?0x26264:0x26144);
    unsigned stop=body?(edition?0x26324:0x26204):STOP;
    reset(entry,STOP);m68k_set_reg(M68K_REG_A0,HERO);m68k_set_reg(M68K_REG_A1,HERO);
    m68k_set_reg(M68K_REG_D0,body?(unsigned)(uint16_t)dx:input);
    m68k_set_reg(M68K_REG_D2,face);w8(HERO+0xc,phase);finish(stop);
    if(body)mask=m68k_get_reg(NULL,M68K_REG_D0)&65535;
    if(mode==2)for(unsigned i=0;i<10;i++)if(eligible[i]){
        unsigned a=r32(display()+i*50+0x18);w16(a+0x3a,originals[i][0]);w16(a+0x3c,originals[i][1]);
    }
}
static void pair(unsigned input,unsigned phase,int body,int dx,int face){
    run(0,input,phase,body,dx,face);normal_mask=mask;
    run(2,input,phase,body,dx,face);
    memcpy(expected,g_ram,RAM_SIZE);
    for(unsigned i=0;i<SAVE_NREGS;i++)expected_regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
    unsigned oracle_mask=mask;
    run(1,input,phase,body,dx,face);
    assert(mask==oracle_mask);
    if(memcmp(expected,g_ram,RAM_SIZE)){
        for(unsigned i=0;i<RAM_SIZE;i++)if(expected[i]!=g_ram[i]){fprintf(stderr,"RAM mismatch e%d at%x expected%02x got%02x\n",edition,i,expected[i],g_ram[i]);break;}abort();
    }
    for(unsigned i=0;i<SAVE_NREGS;i++)assert(expected_regs[i]==m68k_get_reg(NULL,SAVE_REGS[i]));
    /* The correction must never reject a direction accepted by the original. */
    if((normal_mask&mask)!=normal_mask){
        fprintf(stderr,"new block e%d body%d input%x phase%u before%x after%x hero=%d,%d\n",edition,body,input,phase,normal_mask,mask,(int16_t)r16(HERO+4),(int16_t)r16(HERO+8));
        for(unsigned i=0;i<10;i++)if(eligible[i]){unsigned a=r32(display()+i*50+0x18);fprintf(stderr,"foe=%d,%d box=%d,%d standing=%d,%d anchor%d\n",(int16_t)r16(a+4),(int16_t)r16(a+8),(int16_t)r16(a+0x3a),(int16_t)r16(a+0x3c),idlebox[i][r8(a+0xa)==3][0],idlebox[i][r8(a+0xa)==3][1],anchors[i][0]);}
    }
    assert((normal_mask&mask)==normal_mask);
    if(normal_mask!=mask)improved++;
    pairs++;if(body)body_cases++;
}
static void translate(unsigned a,int x,int y){
    int dx=x-(int16_t)r16(a+4),dy=y-(int16_t)r16(a+8);
    w16(a+4,x);w16(a+8,y);
    w16(a+0x3a,r16(a+0x3a)+dx);w16(a+0x3c,r16(a+0x3c)+dx);
    w16(a+0x70,r16(a+0x70)+dy);w16(a+0x72,r16(a+0x72)+dy);
}
static void grid(void){
    memcpy(initial,base,RAM_SIZE);
    static const int xs[]={-96,-72,-48,-32,-16,-8,0,8,16,32,48,72,96};
    static const int ys[]={-21,-20,-11,-10,-4,0,4,10,11,20,21};
    unsigned victim=0;
    for(unsigned i=0;i<10;i++)if(eligible[i])victim=i+1;
    if(!victim)return;
    unsigned slot=victim-1;
    for(unsigned x=0;x<13;x++)for(unsigned y=0;y<11;y++)for(unsigned f=0;f<2;f++)for(unsigned second=0;second<2;second++){
        memcpy(g_ram,initial,RAM_SIZE);
        unsigned a=r32(display()+50*slot+0x18);int oldface=r8(a+0xa);
        if(oldface!=(f?3:1)){
            int ax=(int16_t)r16(a+4),l=(int16_t)r16(a+0x3a),r=(int16_t)r16(a+0x3c);
            w16(a+0x3a,2*ax-r);w16(a+0x3c,2*ax-l);
        }
        w8(a+0xa,f?3:1);translate(HERO,160,100);translate(a,160+xs[x],100+ys[y]);
        for(unsigned i=0;i<10;i++){
            unsigned d=display()+50*i,b=r32(d+0x18);
            if(r8(d) && b!=HERO && b!=a){
                if(!second)w8(d,0);else translate(b,160-xs[x],100);
            }
        }
        memcpy(base,g_ram,RAM_SIZE);
        for(unsigned side=0;side<2;side++){
            pair(0,0,1,side?8:-8,side?1:3);
            /* Ordinary forward movement must still stop at the body, including
             * both recoil facings and presence/absence of a second Trogg. */
            int prospective_left=(int16_t)r16(HERO+0x3a)+(side?8:-8);
            int prospective_right=(int16_t)r16(HERO+0x3c)+(side?8:-8);
            int center=(int16_t)r16(a+4);
            if(!ys[y] && ((xs[x]>0 && side==1) || (xs[x]<0 && side==0))
                && prospective_left<center && prospective_right>center){
                assert(!(mask&(side?1u:2u)));solid_cases++;
            }
        }
    }
    memcpy(base,initial,RAM_SIZE);
}
static void scope(void){
    unsigned slot=0;
    for(unsigned i=0;i<10;i++)if(eligible[i])slot=i+1;
    assert(slot);slot--;
    unsigned d=display()+slot*50;
    for(unsigned site=0;site<2;site++)for(unsigned flags=0;flags<32;flags++)for(unsigned test=0;test<49;test++){
        memcpy(g_ram,base,RAM_SIZE);reset(STOP,STOP);
        unsigned pc=(site?0x29610u:0x295ccu)-(edition?0x14cu:0u),a=r32(d+0x18);
        unsigned bank=r32(a+0x26),image=r32(bank),idle=edition?0x32c9e:0x32eee;
        w32(STACK,edition?0x29414:0x29560);w32(STACK+4,edition?0x26324:0x26204);
        for(unsigned i=0;i<15;i++)m68k_set_reg((m68k_register_t)(M68K_REG_D0+i),0x76540000+i);
        m68k_set_reg(M68K_REG_A0,HERO);m68k_set_reg(M68K_REG_A1,a);m68k_set_reg(M68K_REG_A6,d);
        m68k_set_reg(M68K_REG_D2,0xaabb0000u|r16(a+0x3a));m68k_set_reg(M68K_REG_D3,0xccdd0000u|r16(a+0x3c));
        m68k_set_reg(M68K_REG_SR,0x2700|flags);g_retail_parity=1;
        switch(test){
          case 1:g_os=0;break;
          case 2:g_lineage=LIN_UNKNOWN;break;
          case 3:g_lineage=edition?LIN_CRACKED:LIN_RETAIL;break;
          case 4:pc+=2;break;
          case 5:w16(pc,0x4e71);break;
          case 6:w32(pc+2,0x297e0);break;
          case 7:w16(pc-8,0x2429);break;
          case 8:m68k_set_reg(M68K_REG_SP,STACK+1);break;
          case 9:m68k_set_reg(M68K_REG_SP,RAM_SIZE-4);break;
          case 10:w32(STACK,STOP);break;
          case 11:w32(STACK+4,STOP);break;
          case 12:m68k_set_reg(M68K_REG_A0,0);break;
          case 13:m68k_set_reg(M68K_REG_A0,HERO+1);break;
          case 14:m68k_set_reg(M68K_REG_A0,RAM_SIZE-0x80);break;
          case 15:m68k_set_reg(M68K_REG_A0,a);break;
          case 16:w8(HERO+0x4d,0x10);break;
          case 17:w16(HERO+0x50,0);break;
          case 18:w32(edition?0x2e9ac:0x2ebd0,a);break;
          case 19:m68k_set_reg(M68K_REG_A6,display()-50);break;
          case 20:m68k_set_reg(M68K_REG_A6,display()+500);break;
          case 21:m68k_set_reg(M68K_REG_A6,d+2);break;
          case 22:w8(d,0);break;
          case 23:w32(d+0x18,HERO);break;
          case 24:w32(d+0x18,0);m68k_set_reg(M68K_REG_A1,0);break;
          case 25:w32(d+0x18,a+1);m68k_set_reg(M68K_REG_A1,a+1);break;
          case 26:w32(d+0x18,RAM_SIZE-0x80);m68k_set_reg(M68K_REG_A1,RAM_SIZE-0x80);break;
          case 27:w8(a+0x4d,0x20);break;
          case 28:w16(a+0x50,0xffff);break;
          case 29:w32(d+2,idle);break;
          case 30:w32(d+2,idle+0x372);break;
          case 31:w32(d+2,r32(d+2)+1);break;
          case 32:w32(a+0x16,idle+2);break;
          case 33:w8(idle,4);break;
          case 34:w32(a+0x1e,RAM_SIZE-4);break;
          case 35:w32(r32(a+0x1e)+4,idle);break;
          case 36:w32(a+0x26,0);break;
          case 37:w32(a+0x26,RAM_SIZE-4);break;
          case 38:w32(d+0x1c,bank+4);break;
          case 39:w8(a+0xa,0);break;
          case 40:w32(bank,0);break;
          case 41:w32(bank,image+1);break;
          case 42:w16(image+14,0);break;
          case 43:w16(image+14,321);break;
          case 44:w16(a+0x3a,1000);w16(a+0x3c,1100);break;
          case 45:w8(d+1,0);break;
          case 46:g_retail_parity=0;break;
          case 47:{
            unsigned h=0x2e7dc+0x84*(flags%4);memcpy(g_ram+h,g_ram+HERO,0x84);
            m68k_set_reg(M68K_REG_A0,h);w32(edition?0x2e9ac:0x2ebd0,h);break;
          }
          case 48:w16(a+0x3a,0xfff1);w16(a+0x3c,56);break;
        }
        memcpy(expected,g_ram,RAM_SIZE);
        for(unsigned i=0;i<SAVE_NREGS;i++)expected_regs[i]=m68k_get_reg(NULL,SAVE_REGS[i]);
        if(!test || (test>=45 && test<=47)){
            unsigned f=r8(a+0xa)==3;int left=idlebox[slot][f][0],right=idlebox[slot][f][1];
            if(left<(int16_t)r16(a+0x3a))left=(int16_t)r16(a+0x3a);
            if(right>(int16_t)r16(a+0x3c))right=(int16_t)r16(a+0x3c);
            expected_regs[2]=0xaabb0000u|(uint16_t)left;expected_regs[3]=0xccdd0000u|(uint16_t)right;
        }
        trogg_movement_hook(pc);
        assert(!memcmp(expected,g_ram,RAM_SIZE));
        for(unsigned i=0;i<SAVE_NREGS;i++)if(expected_regs[i]!=m68k_get_reg(NULL,SAVE_REGS[i])){
            fprintf(stderr,"scope mismatch e%d test%u flags%u site%u reg%u wanted%x actual%x\n",edition,test,flags,site,i,expected_regs[i],m68k_get_reg(NULL,SAVE_REGS[i]));exit(87);
        }
        scope_cases++;
    }
}
static void edges(void){
    memcpy(initial,base,RAM_SIZE);
    static const int xs[]={0,8,16,40,160,280,304,312,320};
    static const int ys[]={50,60,90,130,150,180};
    static const unsigned dirs[]={1,2,4,8,5,6,9,10};
    for(unsigned x=0;x<9;x++)for(unsigned y=0;y<6;y++)for(unsigned side=0;side<2;side++){
        memcpy(g_ram,initial,RAM_SIZE);translate(HERO,xs[x],ys[y]);
        for(unsigned i=0;i<10;i++){
            unsigned d=display()+50*i,a=r32(d+0x18);
            if(r8(d) && a!=HERO)translate(a,xs[x]+(eligible[i]?(side?40:-40):(side?-50:50)),ys[y]+4);
        }
        memcpy(base,g_ram,RAM_SIZE);
        for(unsigned dir=0;dir<8;dir++)for(unsigned phase=0;phase<4;phase++)pair(dirs[dir],phase,0,0,0);
    }
    memcpy(base,initial,RAM_SIZE);
}
int main(int argc,char **argv){
    assert(argc==5 && !strcmp(argv[1],"--native") && !strcmp(argv[3],"--log"));
    g_log=fopen(argv[4],"w");assert(g_log);m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    char path[1400],name[200];snprintf(path,sizeof(path),"%s/cases.tsv",argv[2]);
    FILE *list=fopen(path,"r");assert(list);unsigned input,phase,states=0;
    while(fscanf(list,"%199s %u %u",name,&input,&phase)==3){
        for(edition=0;edition<2;edition++){
            snprintf(path,sizeof(path),"%s/%s-%s.ram",argv[2],name,edition?"retail":"earlier");
            FILE *f=fopen(path,"rb");assert(f);assert(fread(base,1,RAM_SIZE,f)==RAM_SIZE);fclose(f);
            prepare_boxes();
            for(unsigned v=0;v<33;v++){
                static const unsigned dirs[]={1,2,4,8,5,6,9,10};
                pair(v?dirs[(v-1)/4]:input,v?(v-1)%4:phase,0,0,0);
            }
            if(strstr(name,"axe") && strstr(name,"01-input") && !strncmp(name,"reaction-",9))grid();
            if(!strcmp(name,"reaction-axe-0rf-d-01-input-fr38")){scope();edges();}
        }
        states++;fprintf(g_log,"PASS %s\n",name);fflush(g_log);
    }
    fclose(list);printf("PASS states=%u full native pairs=%u body cases=%u improved=%u scope=%u solid=%u; complete RAM/CPU oracle agreement and no new blocked directions\n",states,pairs,body_cases,improved,scope_cases,solid_cases);
    fclose(g_log);return 0;
}
