#ifndef USURPER_SPARSE_RING_H
#define USURPER_SPARSE_RING_H
#include "usurper_scene.h"
#include "usurper_sparse_rings_generated.h"
/* CRC is checked by the existing full atlas decoder. Its dimension/CRC tuple
 * and each region's coordinates bind these rectangles to the generated asset;
 * the generator and delivery manifest additionally bind the compressed SHA256. */
static inline const us_sparse_ring_t *us_sparse_ring_find(uint32_t crc,unsigned width,unsigned height,
    unsigned radius,unsigned x,unsigned y,unsigned w,unsigned h){
    if(crc!=US_SPARSE_ATLAS_CRC||width!=US_SPARSE_ATLAS_WIDTH||height!=US_SPARSE_ATLAS_HEIGHT)return 0;
    for(unsigned i=0;i<US_SPARSE_RING_COUNT;++i){const us_sparse_ring_t *r=&us_sparse_rings[i];
        if(r->radius==radius&&r->x==x&&r->y==y&&r->w==w&&r->h==h&&r->count&&r->rects)return r;
    }return 0;
}
/* 0: emitted (including offscreen), -1: emitter failed, -2: unsupported and
 * nothing emitted. Rectangles are disjoint; omitted pixels have alpha zero.
 * Preflight avoids rendering a partial candidate followed by full fallback. */
static inline int us_sparse_ring_emit(const us_sparse_ring_t *r,const ts2d_command_t *whole,
    usurper_emit_fn emit,void *context){
    if(!r||!r->rects||!r->count||r->count>64u||!whole||!emit||
       whole->opcode!=TS2D_OP_BLIT_ALPHA_PIXEL||whole->src_format!=TS2D_FMT_ARGB4444||
       (whole->flags&~15u)||!(whole->flags&TS2D_FLAG_CLIP_TO_VIEWPORT)||
       !(whole->flags&TS2D_FLAG_SRC_ARGB4444)||whole->src_x<0||whole->src_y<0||
       whole->width!=r->w||whole->height!=r->h)return -2;
    for(unsigned i=0;i<r->count;++i){const us_sparse_rect_t *a=&r->rects[i];
        if(!a->w||!a->h||a->x+a->w>r->w||a->y+a->h>r->h||
           (unsigned)whole->src_x+a->x>32767u||
           (uint64_t)whole->src_base_addr+((unsigned)whole->src_y+a->y)*(uint64_t)whole->src_stride_bytes>0xffffffffu)return -2;
        for(unsigned j=0;j<i;++j){const us_sparse_rect_t *b=&r->rects[j];
            if(a->x<b->x+b->w&&b->x<a->x+a->w&&a->y<b->y+b->h&&b->y<a->y+a->h)return -2;
        }
    }
    for(unsigned i=0;i<r->count;++i){const us_sparse_rect_t *a=&r->rects[i];ts2d_command_t c=*whole;
        int x=whole->dst_x+((whole->flags&TS2D_FLAG_FLIP_X)?r->w-a->x-a->w:a->x);
        int y=whole->dst_y+((whole->flags&TS2D_FLAG_FLIP_Y)?r->h-a->y-a->h:a->y);
        if(x>=1024||y>=576||x+(int)a->w<=0||y+(int)a->h<=0)continue;
        unsigned sy=(unsigned)whole->src_y+a->y;
        c.src_x=(int16_t)(whole->src_x+a->x);
        if(sy>32767u){c.src_base_addr+=(uint32_t)sy*c.src_stride_bytes;c.src_y=0;}
        else c.src_y=(int16_t)sy;
        c.dst_x=(int16_t)x;c.dst_y=(int16_t)y;c.width=a->w;c.height=a->h;
        if(emit(context,&c))return -1;
    }return 0;
}
#endif
