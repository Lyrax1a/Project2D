/* SPDX-License-Identifier: MIT */
#include "usurper_export.h"
#ifndef US_EXPORT_NO_UART
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
#endif

static void delay_ticks(us_export_t *s,uint64_t duration) {
    uint64_t start=s->io->ticks(s->io->context);
    while(s->io->ticks(s->io->context)-start<duration)s->io->service(s->io->context);
}
static void delay_ms(us_export_t *s,unsigned ms) {
    delay_ticks(s,((uint64_t)s->io->hz*ms+999u)/1000u);
}
static void physical_flush(us_export_t *s) {
    s->io->flush(s->io->context);
    /* uart_flush only empties its SW ring. At most 255 FIFO bytes plus one
     * serializer remain. 260 complete 8N1 characters conservatively cover all
     * of them; it also works with a 4 KiB software queue. The actual Sapphire
     * 115200/460800 divisors run slightly faster than these nominal rates.
     */
    delay_ticks(s,((uint64_t)s->io->hz*2600u+s->baud-1u)/s->baud);
}
static void text(us_export_t *s,const char *value) {
    while(*value)s->io->transmit(s->io->context,(uint8_t)*value++);
}
static void change_baud(us_export_t *s,uint32_t baud) {
    s->io->set_baud(s->io->context,baud);s->baud=baud;
}
static int receive_until(us_export_t *s,uint64_t start) {
    uint64_t timeout=(uint64_t)s->io->hz*US_EXPORT_TIMEOUT_MS/1000u;
    while(s->io->ticks(s->io->context)-start<timeout) {
        int c=s->io->receive(s->io->context);
        if(c>=0)return c;
        s->io->service(s->io->context);
    }
    return -1;
}
static int fail(us_export_t *s,int error,const char *message) {
    physical_flush(s);
    /* The host switches back immediately after ACK/NAK. This quiet interval
     * avoids transmitting the first base-baud error byte during that switch.
     */
    delay_ms(s,US_EXPORT_SETTLE_MS);
    change_baud(s,US_EXPORT_BASE_BAUD);
    s->active=0;s->error=error;
    text(s,message);physical_flush(s);
    /* A partial/invalid token cannot leak queued command bytes to game input. */
    for(unsigned n=0;n<256u&&s->io->receive(s->io->context)>=0;++n){}
    return error;
}
int us_export_begin(us_export_t *s,const us_export_io_t *io) {
    static const char token[]="TS2DFAST";
    if(!s||!io||!io->ticks||!io->receive||!io->transmit||!io->flush||
       !io->service||!io->set_baud||!io->hz)return US_EXPORT_ARGUMENT;
    if(s->active)return US_EXPORT_STATE;
    s->io=io;s->baud=US_EXPORT_BASE_BAUD;s->error=0;
    physical_flush(s);
    uint64_t start=io->ticks(io->context);
    unsigned bad=0;
    for(unsigned i=0;i<8;++i) {
        int c=receive_until(s,start);
        if(c<0)return fail(s,US_EXPORT_TOKEN_TIMEOUT,"XD_ERROR phase=token reason=timeout\r\n");
        if(c!=(uint8_t)token[i])bad=1;
    }
    if(bad)return fail(s,US_EXPORT_TOKEN_INVALID,"XD_ERROR phase=token reason=invalid\r\n");
    change_baud(s,US_EXPORT_BAUD);
    start=io->ticks(io->context);
    int c=receive_until(s,start);
    if(c<0)return fail(s,US_EXPORT_SYNC_TIMEOUT,"XD_ERROR phase=sync reason=timeout\r\n");
    if(c!=0x55)return fail(s,US_EXPORT_SYNC_INVALID,"XD_ERROR phase=sync reason=invalid\r\n");
    s->active=1;text(s,"XD_READY\r\n");io->flush(io->context);
    return US_EXPORT_OK;
}
int us_export_finish(us_export_t *s) {
    if(!s||!s->io||!s->active)return US_EXPORT_STATE;
    physical_flush(s);
    int c=receive_until(s,s->io->ticks(s->io->context));
    if(c<0)return fail(s,US_EXPORT_ACK_TIMEOUT,"XD_ERROR phase=ack reason=timeout\r\n");
    if(c==0x15)return fail(s,US_EXPORT_HOST_REJECTED,"XD_ERROR phase=ack reason=host_rejected\r\n");
    if(c!=0x06)return fail(s,US_EXPORT_ACK_INVALID,"XD_ERROR phase=ack reason=invalid\r\n");
    delay_ms(s,US_EXPORT_SETTLE_MS);
    change_baud(s,US_EXPORT_BASE_BAUD);s->active=0;
    text(s,"XD_DONE\r\n");physical_flush(s);
    return US_EXPORT_OK;
}
#ifndef US_EXPORT_NO_UART
static uint64_t board_ticks(void *p){(void)p;return now64();}
static int board_receive(void *p){(void)p;return getch();}
static void board_transmit(void *p,uint8_t c){(void)p;putch((char)c);}
static void board_flush(void *p){(void)p;uart_flush();}
static void board_service(void *p){(void)p;uart_service();}
static void board_baud(void *p,uint32_t baud){(void)p;UART(8)=CPU_HZ/(baud*8u)-1u;}
static const us_export_io_t board_io={0,board_ticks,board_receive,board_transmit,
    board_flush,board_service,board_baud,CPU_HZ};
static us_export_t board_session;
int usurper_export_begin(void){return us_export_begin(&board_session,&board_io);}
int usurper_export_finish(void){return us_export_finish(&board_session);}
#endif
