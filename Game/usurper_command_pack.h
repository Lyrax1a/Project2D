#ifndef USURPER_COMMAND_PACK_H
#define USURPER_COMMAND_PACK_H
#include "../Engine/Software/sw/include/ts2d_command_defs.h"
/* The TS2D little-endian ABI is eight 32-bit words. Form each word before
 * storing it on this write-through RV32 core, avoiding repeated halfword
 * writes to the same stack word. The union retains the typed emitter API. */
typedef union {ts2d_command_t command;uint32_t words[8];} us_command_words_t;
static inline us_command_words_t us_command_pack(
    uint8_t opcode,uint8_t flags,uint8_t format,uint8_t alpha,
    uint32_t source,uint32_t destination,int16_t sx,int16_t sy,
    int16_t dx,int16_t dy,uint16_t width,uint16_t height,
    uint16_t source_stride,uint16_t destination_stride,uint32_t parameter){
    return (us_command_words_t){.words={
        (uint32_t)opcode|((uint32_t)flags<<8)|((uint32_t)format<<16)|((uint32_t)alpha<<24),
        source,destination,(uint16_t)sx|((uint32_t)(uint16_t)sy<<16),
        (uint16_t)dx|((uint32_t)(uint16_t)dy<<16),width|((uint32_t)height<<16),
        source_stride|((uint32_t)destination_stride<<16),parameter}};
}
#endif
