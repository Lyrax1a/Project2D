#include "usurper_baked_assets.h"
#ifdef USURPER_WARDEN_BAKED
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
#include "usurper_asset_codec.h"
#include "usurper_warden_baked.h"
#include "usurper_cpu_renderer.h"
#include "../Assets/Generated/usurper_baked_assets_config.h"

static uint32_t crc32(const volatile uint8_t *bytes,unsigned size){
    uint32_t crc=~0u;
    while(size--){crc^=*bytes++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}
    return ~crc;
}
static int read_byte(void){
    uint64_t limit=now64()+(uint64_t)CPU_HZ*180u;int c;
    do{c=getch();uart_service();}while(c<0 && now64()<limit);return c;
}
int usurper_baked_assets_load(void){
    volatile uint8_t *input=(volatile uint8_t *)(uintptr_t)US_BAKED_TRANSFER_BASE;
    volatile uint8_t *output=(volatile uint8_t *)(uintptr_t)US_BAKED_DECODE_BASE;
    if(!US_BAKED_COMPRESSED_BYTES || US_BAKED_COMPRESSED_BYTES>US_BAKED_TRANSFER_LIMIT ||
       !US_BAKED_RAW_BYTES || US_BAKED_RAW_BYTES>US_BAKED_DECODE_LIMIT)return -1;
    puts_uart("WARDEN_ASSET_READY\r\n");uart_flush();
    for(;;){
        uint8_t header[12];
        for(unsigned i=0;i<sizeof(header);i++){int c=read_byte();if(c<0)return -1;header[i]=(uint8_t)c;}
        unsigned cached=header[2]=='C' && header[3]=='K';
        if(header[0]!='W'||header[1]!='B'||(!cached && !(header[2]=='L' && header[3]=='4')))return -1;
        uint32_t bytes=0,expected=0;
        for(unsigned i=0;i<4;i++){bytes|=(uint32_t)header[4+i]<<(8*i);expected|=(uint32_t)header[8+i]<<(8*i);}
        if(bytes!=US_BAKED_COMPRESSED_BYTES || expected!=US_BAKED_COMPRESSED_CRC)return -1;
        if(!cached)for(unsigned i=0;i<bytes;i++){int c=read_byte();if(c<0)return -1;input[i]=(uint8_t)c;}
        drain();invalidate();
        if(crc32(input,bytes)!=expected){
            if(!cached)return -1;
            puts_uart("WARDEN_ASSET_CACHE=MISS\r\n");uart_flush();continue;
        }
        if(cached){puts_uart("WARDEN_ASSET_CACHE=PASS\r\n");uart_flush();}
        break;
    }
    unsigned produced=0;
    if(us_lz4_decode(input,US_BAKED_COMPRESSED_BYTES,output,US_BAKED_RAW_BYTES,&produced)||
       produced!=US_BAKED_RAW_BYTES || crc32(output,produced)!=US_BAKED_RAW_CRC)return -1;
    drain();invalidate();
    if(us_warden_baked_attach((const void *)(uintptr_t)US_BAKED_DECODE_BASE,produced))return -1;
    /* Attach has validated the immutable image directory. Register only its
     * pixel payload; commands must never use the header/table as source pixels. */
    if(usurper_cpu_register_packed_source(us_warden_baked_pixel_base(),us_warden_baked_pixel_bytes()))return -1;
    puts_uart("WARDEN_ASSET_LOAD=PASS BYTES=");number(produced);puts_uart(" CRC32=");number(US_BAKED_RAW_CRC);puts_uart("\r\n");uart_flush();
    return 0;
}
#else
int usurper_baked_assets_load(void){return 0;}
#endif
