#include "usurper_warden_baked.h"
#include "usurper_warden_baked_ids.h"
#include "usurper_atan.h"

typedef struct {uint32_t first;uint16_t count;uint8_t alpha,reserved;} image_t;
typedef struct {uint32_t offset;int16_t x,y;uint16_t width,height;uint32_t reserved;} rectangle_t;
static const uint8_t *blob;
static const image_t *images;
static const rectangle_t *rectangles;
static uint32_t image_count,rectangle_count,pixels_offset,blob_bytes;
_Static_assert(sizeof(image_t)==8,"Baked image ABI");
_Static_assert(sizeof(rectangle_t)==16,"Baked rectangle ABI");
int us_warden_baked_attach(const void *memory,size_t bytes){
    blob=0;images=0;rectangles=0;image_count=rectangle_count=pixels_offset=blob_bytes=0;
    if(!memory||((uintptr_t)memory&3u)||bytes<32u||bytes>24u*1024u*1024u)return -1;
    const uint32_t *header=memory;
    unsigned expected=header[2]==US_WB_FIELD_DETAILS+US_WB_FIELD_DETAILS_COUNT;
#ifdef US_WB_FORM_DASH
    expected|=header[2]==US_WB_FORM_DASH+US_WB_FORM_DASH_COUNT;
#endif
#ifdef US_WB_FIELD92_DETAILS
    expected|=header[2]==US_WB_FIELD92_DETAILS+US_WB_FIELD92_DETAILS_COUNT;
#endif
    if(header[0]!=0x4c425755u||header[1]!=1||!expected||header[4]!=32||header[7]!=bytes)return -1;
    if(header[2]>(bytes-32)/8u||header[5]!=32+header[2]*8u||header[3]>(bytes-header[5])/16u||header[6]!=header[5]+header[3]*16u)return -1;
    const image_t *im=(const image_t *)((const uint8_t *)memory+header[4]);
    const rectangle_t *rect=(const rectangle_t *)((const uint8_t *)memory+header[5]);
    for(unsigned i=0;i<header[2];++i)if(im[i].reserved||im[i].first>header[3]||im[i].count>header[3]-im[i].first)return -1;
    for(unsigned i=0;i<header[3];++i){
        if(rect[i].reserved||!rect[i].width||!rect[i].height||rect[i].width>320||rect[i].height>320||
           (rect[i].offset&1u)||rect[i].offset>bytes-header[6]||
           (uint32_t)rect[i].width*rect[i].height*2u>bytes-header[6]-rect[i].offset)return -1;
    }
    blob=memory;images=im;rectangles=rect;image_count=header[2];rectangle_count=header[3];pixels_offset=header[6];blob_bytes=header[7];return 0;
}
unsigned us_warden_baked_ready(void){return blob!=0;}
size_t us_warden_baked_bytes(void){return blob_bytes;}
uint32_t us_warden_baked_pixel_base(void){return blob?(uint32_t)(uintptr_t)(blob+pixels_offset):0;}
size_t us_warden_baked_pixel_bytes(void){return blob?blob_bytes-pixels_offset:0;}
int us_warden_baked_image(unsigned image,int x,int y,unsigned opacity,uint32_t framebuffer,usurper_emit_fn emit,void *context){
    if(!blob||image>=image_count||!emit||opacity>255)return -1;
    const image_t *im=&images[image];unsigned alpha=(im->alpha*opacity+127u)/255u;
    if(!alpha)return 0;
    for(unsigned i=0;i<im->count;++i){const rectangle_t *r=&rectangles[im->first+i];int dx=x+r->x,dy=y+r->y;
        if(dx+(int)r->width<=0||dy+(int)r->height<=0||dx>=(int)USURPER_GPU_WIDTH||dy>=(int)USURPER_GPU_HEIGHT)continue;
        if(dx<-32768||dy<-32768||dx>32767||dy>32767)return -1;
        ts2d_command_t command={.opcode=TS2D_OP_BLIT_ALPHA_PIXEL,.flags=TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,
            .src_format=TS2D_FMT_ARGB4444,.global_alpha=(uint8_t)alpha,.src_base_addr=(uint32_t)(uintptr_t)(blob+pixels_offset+r->offset),
            .dst_base_addr=framebuffer,.dst_x=(int16_t)dx,.dst_y=(int16_t)dy,.width=r->width,.height=r->height,
            .src_stride_bytes=(uint16_t)(r->width*2),.dst_stride_bytes=USURPER_GPU_STRIDE};
        if(emit(context,&command))return -1;
    }
    return 0;
}
static unsigned progress_index(double progress){return progress<=0?0:progress>=1?64:(unsigned)(progress*64+0.5);}
static unsigned angle_index(float radians){
    int result=(int)(radians*14.32394487827058f+(radians<0?-0.5f:0.5f));result%=90;if(result<0)result+=90;return (unsigned)result;
}
int us_warden_baked_hull(const us_warden_hull_view_t *v,int x,int y,uint32_t framebuffer,usurper_emit_fn emit,void *context){
    if(!blob||v->scale<0.999f||v->scale>1.101f||v->radius!=80)return -2;
    if(v->controlled.r!=0.16f||v->controlled.g!=0.7f||v->controlled.b!=1)return -2;
    unsigned scale=(unsigned)((v->scale-1)*20+0.5f);if(scale>2)scale=2;
    unsigned progress=v->material?progress_index(v->progress):0,flash=!!v->flash;
    unsigned id;
    if(v->supply)id=US_WB_SUPPLY+flash*3+scale;
    else if(!v->alive&&v->material&&v->progress>=0.999){if(flash)return -2;id=US_WB_CONTROLLED+scale*90+angle_index(v->rotation);}
    else {if(v->rotation!=0)return -2;id=US_WB_ENEMY+flash*195+scale*65+progress;}
    if(us_warden_baked_image(id,x,y,255,framebuffer,emit,context))return -1;
    return us_warden_baked_aura(v,x,y,framebuffer,emit,context);
}
int us_warden_baked_aura(const us_warden_hull_view_t *v,int x,int y,uint32_t framebuffer,usurper_emit_fn emit,void *context){
    if(!blob||v->scale<0.999f||v->scale>1.101f||v->radius!=80)return -2;
    unsigned scale=(unsigned)((v->scale-1)*20+0.5f);if(scale>2)scale=2;
    unsigned progress=v->material?progress_index(v->progress):0,flash=!!v->flash;
    if(v->disruption>0||!v->alive||v->supply||v->icon)return 0;
    if(us_warden_baked_image(US_WB_AURA_FILL+flash*3+scale,x,y,255,framebuffer,emit,context)||
       us_warden_baked_image(US_WB_AURA_BORDER+flash*3+scale,x,y,255,framebuffer,emit,context))return -1;
    if(v->material&&v->progress>0){
        if(us_warden_baked_image(US_WB_CAPTURE_SECTOR+scale*65+progress,x,y,255,framebuffer,emit,context)||
           us_warden_baked_image(US_WB_CAPTURE_ARC+scale*65+progress,x,y,255,framebuffer,emit,context))return -1;
    }
    return 0;
}
int us_warden_baked_field(const us_warden_field_t *v,int x,int y,uint32_t framebuffer,usurper_emit_fn emit,void *context){
    if(!blob||v->duration<=0)return -2;
    if(!v->active)return 0;
    unsigned radius=v->radius==82;
    unsigned fill=US_WB_FIELD_FILL+radius,border=US_WB_FIELD_STATIC+radius;
    unsigned block=US_WB_FIELD_BLOCK+radius*90,details=US_WB_FIELD_DETAILS+radius;
    if(v->radius!=72&&v->radius!=82){
#ifdef US_WB_FIELD92_DETAILS
        if(v->radius!=92||image_count<US_WB_FIELD92_DETAILS+US_WB_FIELD92_DETAILS_COUNT)return -2;
        fill=US_WB_FIELD92_FILL;border=US_WB_FIELD92_STATIC;
        block=US_WB_FIELD92_BLOCK;details=US_WB_FIELD92_DETAILS;
#else
        return -2;
#endif
    }
    if(us_warden_baked_image(fill,x,y,255,framebuffer,emit,context)||
       us_warden_baked_image(border,x,y,255,framebuffer,emit,context)||
       us_warden_baked_image(US_WB_FIELD_COUNTDOWN+progress_index(1-v->age/v->duration),x,y,255,framebuffer,emit,context))return -1;
    if(v->block_flash>0){unsigned alpha=v->block_flash>=0.24?255:(unsigned)(v->block_flash*(255/0.24)+0.5);
        if(us_warden_baked_image(block+angle_index(us_atan_float(v->block_dx,v->block_dy)),x,y,alpha,framebuffer,emit,context))return -1;}
    return us_warden_baked_image(details,x,y,255,framebuffer,emit,context);
}
int us_warden_baked_marker(unsigned front,int x,int y,uint32_t framebuffer,usurper_emit_fn emit,void *context){
    return us_warden_baked_image(US_WB_MARKER+!!front,x,y,255,framebuffer,emit,context);
}
int us_warden_baked_form(int form,unsigned angle,int shielded,int hurt,
    double cooldown,double guard_flash,int dashing,int x,int y,
    uint32_t framebuffer,usurper_emit_fn emit,void *context){
#ifdef US_WB_FORM_DASH
    if(!blob||image_count<US_WB_FORM_DASH+US_WB_FORM_DASH_COUNT)return -2;
    unsigned index=form==0?0u:form==1?1u:form==2?2u:form==6?3u:form==9?4u:5u;
    if(index==5u)return -2;
    unsigned a=((angle%360u+2u)/4u)%90u;
    if(us_warden_baked_image(US_WB_FORM_BASE+index*180u+!!hurt*90u+a,x,y,255,framebuffer,emit,context))return -1;
    if((form==1||form==9)&&cooldown<=0){
        if(us_warden_baked_image(US_WB_FORM_READY+(form==9?90u:0u)+a,x,y,255,framebuffer,emit,context))return -1;
    }else if(form==9){
        if(us_warden_baked_image(US_WB_FORM_COOLDOWN+a,x,y,255,framebuffer,emit,context))return -1;
    }
    if(shielded&&us_warden_baked_image(US_WB_FORM_SHIELD+a,x,y,255,framebuffer,emit,context))return -1;
    if(guard_flash>0){unsigned opacity=guard_flash>=.25?255u:(unsigned)(guard_flash*1020+.5);
        if(us_warden_baked_image(US_WB_FORM_GUARD+a,x,y,opacity,framebuffer,emit,context))return -1;
    }
    if(dashing&&us_warden_baked_image(US_WB_FORM_DASH+a,x,y,255,framebuffer,emit,context))return -1;
    return 0;
#else
    (void)form;(void)angle;(void)shielded;(void)hurt;(void)cooldown;(void)guard_flash;
    (void)dashing;(void)x;(void)y;(void)framebuffer;(void)emit;(void)context;return -2;
#endif
}
