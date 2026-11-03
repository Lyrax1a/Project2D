#ifndef USURPER_RESEARCH_ICONS_H
#define USURPER_RESEARCH_ICONS_H
/* Research-only nearest-neighbour scale cache. Each framebuffer owns a bank,
 * so rebuilding an animated icon cannot overwrite a pending GPU source. */
#define US_RESEARCH_ICON_BASE 0x01800000u
#define US_RESEARCH_ICON_BANK_BYTES 0x00200000u
#define US_RESEARCH_ICON_SLOT_BYTES 32768u
#define US_RESEARCH_ICON_SIDE 128u
#ifdef US_RESEARCH_ICON_IMPLEMENTATION
_Static_assert(US_RESEARCH_ICON_BASE>=USURPER_FB_B+USURPER_GPU_STRIDE*USURPER_GPU_HEIGHT,"research cache overlaps framebuffer");
_Static_assert(US_RESEARCH_ICON_BASE+2u*US_RESEARCH_ICON_BANK_BYTES<=USURPER_ATLAS_BASE,"research cache overlaps atlas");
_Static_assert(USURPER_TECH_NODE_COUNT <= 64u,"research icon bank capacity");
static uint8_t research_icon_sizes[2][USURPER_TECH_NODE_COUNT];
static void research_icon(unsigned index,int x,int y,unsigned design_size) {
    unsigned size=scale_design_extent(design_size);
    if(!size||size>US_RESEARCH_ICON_SIDE)return;
    int left=scale_design_coord(x),top=scale_design_coord(y);
    int x0=clamp_i(left,scale_design_coord(219),scale_design_coord(945));
    int y0=clamp_i(top,scale_design_coord(69),scale_design_coord(465));
    int x1=clamp_i(left+(int)size,scale_design_coord(219),scale_design_coord(945));
    int y1=clamp_i(top+(int)size,scale_design_coord(69),scale_design_coord(465));
    if(x1<=x0||y1<=y0)return;
    unsigned bank=current_framebuffer==USURPER_FB_B;
    uint32_t base=US_RESEARCH_ICON_BASE+bank*US_RESEARCH_ICON_BANK_BYTES+index*US_RESEARCH_ICON_SLOT_BYTES;
    if(research_icon_sizes[bank][index]!=size) {
        unsigned icon=usurper_tech_nodes[index].icon_index;
        const volatile uint16_t *src=(const volatile uint16_t *)(uintptr_t)USURPER_ATLAS_BASE;
        volatile uint16_t *dst=(volatile uint16_t *)(uintptr_t)base;
        unsigned sx=tech_icon_x(icon),sy=tech_icon_y(icon),columns[128];
        for(unsigned col=0;col<size;++col)columns[col]=col*TECH_ICON_SIZE/size;
        for(unsigned row=0;row<size;++row) {
            unsigned offset=(sy+row*TECH_ICON_SIZE/size)*(USURPER_ATLAS_STRIDE/2)+sx;
            for(unsigned col=0;col<size;++col)dst[row*128+col]=src[offset+columns[col]];
        }
#ifndef USURPER_HOST
        __asm__ volatile("fence rw,rw" ::: "memory");
#endif
        research_icon_sizes[bank][index]=(uint8_t)size;
    }
    us_command_words_t c=us_command_pack(TS2D_OP_BLIT_ALPHA_PIXEL,
        TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,TS2D_FMT_ARGB4444,255,
        base,current_framebuffer,(int16_t)(x0-left),(int16_t)(y0-top),(int16_t)x0,(int16_t)y0,
        (uint16_t)(x1-x0),(uint16_t)(y1-y0),256,USURPER_GPU_STRIDE,0);
    (void)emit_raw(&c.command);
}
#endif
#endif
