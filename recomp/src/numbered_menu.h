/* Host cursor for the original map's numbered destination popup. The guest still
 * builds its list, draws its wording and dispatches the selected action. Both
 * addresses are copies of Mog code, not separate treasure/menu implementations. */
static struct {
    uint32_t wait;
    int count, selected, previous, live, ready;
} g_numbered;

static void numbered_menu_reset(void) { memset(&g_numbered,0,sizeof(g_numbered)); }

static uint32_t numbered_menu_wait_for_pc(uint32_t pc) {
    uint32_t wait=0;
    const uint32_t sites[]={0x41178u,0x1e476cu,0x40d96u,0x1e43aau};
    for (unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++)
        if (pc>=sites[i]-0x16 && pc<=sites[i]+0x62) { wait=sites[i]; break; }
    if (!g_os || !wait) return 0;
    /* Guard the native rawkey read, ASCII bounds and eight-byte list indexing.
     * These ranges can hold unrelated code before Mog has been loaded. */
    uint32_t raw=g_lineage==LIN_RETAIL ? 0x3bc5c : 0x3bf74;
    uint32_t list=g_lineage==LIN_RETAIL ? 0x2feec : 0x30138;
    return r16(wait)==0x3039 && r32(wait+2)==raw && r16(wait+6)==0x67f8
        && r16(wait+8)==0x4eb9 && r32(wait+14)==0xb07c0031
        && r32(wait+20)==0xb07c0039 && r32(wait+30)==0xc0fc0008
        && r16(wait+34)==0x45f9 && r32(wait+36)==list
        && r16(wait+0x62)==0x2079 ? wait : 0;
}

static int numbered_menu_live(void) {
    return g_numbered.wait && g_numbered.ready
        && g_cur_frame>=g_numbered.live && g_cur_frame-g_numbered.live<=1;
}

static void numbered_menu_hook(uint32_t pc) {
    /* All other instructions take this cheap path. */
    if (pc!=0x41162 && pc!=0x41178 && pc!=0x411da
        && pc!=0x1e4756 && pc!=0x1e476c && pc!=0x1e47ce
        && pc!=0x40d80 && pc!=0x40d96 && pc!=0x40df8
        && pc!=0x1e4394 && pc!=0x1e43aa && pc!=0x1e440c) return;
    uint32_t wait=numbered_menu_wait_for_pc(pc);
    if (!wait) return;
    if (pc==wait+0x62) { numbered_menu_reset(); return; }
    int input=(g_kdigit<<4) | (g_ji_up ? 1 : 0) | (g_ji_dn ? 2 : 0) | (g_fire ? 4 : 0);
    if (pc==wait-0x16 || g_numbered.wait!=wait) {
        numbered_menu_reset();
        uint32_t list=r32(wait+36);
        while (g_numbered.count<9 && r32(list+8*g_numbered.count)) g_numbered.count++;
        if (g_numbered.count<2) return;
        g_numbered.wait=wait;
        g_numbered.selected=1;
        g_numbered.previous=input; /* The map's opening press cannot confirm option one. */
    }
    if (pc!=wait) return;
    g_numbered.live=g_cur_frame;
    g_numbered.ready=1;
    int edges=input & ~g_numbered.previous;
    int digit=(input>>4)!=(g_numbered.previous>>4) ? input>>4 : 0;
    if ((edges&3) && (input&3)!=3) {
        g_numbered.selected+=(input&1) ? -1 : 1;
        if (g_numbered.selected<1) g_numbered.selected=g_numbered.count;
        if (g_numbered.selected>g_numbered.count) g_numbered.selected=1;
    }
    int choice=digit ? digit : (edges&4) ? g_numbered.selected : 0;
    if (choice>=1 && choice<=g_numbered.count) {
        g_popup_injected=(uint16_t)(choice+1);
        w16(r32(wait+2),g_popup_injected);
    }
    g_numbered.previous=input;
}

static void numbered_menu_draw(SDL_Renderer *ren, int width, int height) {
    if (!numbered_menu_live() || width<=0 || height<=0) return;
    uint32_t xy=g_lineage==LIN_RETAIL ? 0x373be : 0x37608;
    /* Original builder leaves Y just below its last row; each row is six pixels.
     * Center the six-pixel highlight around the five-pixel glyphs. */
    int x=(int16_t)r16(xy);
    float y=(int16_t)r16(xy+2)-6*g_numbered.count+6*(g_numbered.selected-1)-0.5f;
    if (x<6 || x>140 || y<0 || y>193) return;
    Uint8 r,g,b,a; SDL_BlendMode blend;
    SDL_GetRenderDrawColor(ren,&r,&g,&b,&a);
    SDL_GetRenderDrawBlendMode(ren,&blend);
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
    /* The 320x200 game is stretched into the renderer's 320x256 logical view.
     * Follow the displayed texture rather than treating guest Y as host Y. */
    int logical_w,logical_h;SDL_RenderGetLogicalSize(ren,&logical_w,&logical_h);
    float sx=(float)logical_w/width,sy=(float)logical_h/height;
    SDL_SetRenderDrawColor(ren,255,231,160,65);
    SDL_FRect row={(x+3)*sx,y*sy,166*sx,6*sy};
    SDL_RenderFillRectF(ren,&row);
    SDL_SetRenderDrawColor(ren,20,12,4,255);
    SDL_FRect shadow={(x-5)*sx,y*sy,7*sx,7*sy};SDL_RenderFillRectF(ren,&shadow);
    SDL_SetRenderDrawColor(ren,255,231,160,255);
    for(int i=0;i<3;i++)SDL_RenderDrawLineF(ren,(x-4+i)*sx,(y+1+i)*sy,(x-4+i)*sx,(y+5-i)*sy);
    SDL_SetRenderDrawColor(ren,r,g,b,a);
    SDL_SetRenderDrawBlendMode(ren,blend);
}
