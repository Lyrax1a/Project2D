/* PC playback transport. No GPU audio registers or PCM generation.
 * UA1=<sequence:u16,phase:u8,flags:u8,music:u8,sfx:u8,mute:u8,ids...,xor:u8>
 * All bytes are uppercase hex; newline delimited, maximum eight events.
 * The asynchronous UART accepts or drops the entire record without waiting. */
#ifndef USURPER_AUDIO_HOST
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
#ifndef GRAVITY_ASYNC_UART
#error PC audio requires the bounded asynchronous UART
#endif
#else
extern int uart_try_write(const char *data,unsigned length);
#endif
static usurper_audio_stats_t statistics;
static usurper_settings_t preferences=US_SETTINGS_DEFAULT;
static unsigned tick,phase,muted,dirty,sequence,last_service,last_state;
static unsigned seen,last[16],count;
static uint8_t events[8];
static void hex_byte(char *out,unsigned value){
    static const char hex[]="0123456789ABCDEF";
    out[0]=hex[(value>>4)&15u];out[1]=hex[value&15u];
}
void usurper_audio_init(void){
    memset(&statistics,0,sizeof(statistics));memset(last,0,sizeof(last));
    preferences=(usurper_settings_t)US_SETTINGS_DEFAULT;
    tick=phase=muted=sequence=seen=count=last_state=0;dirty=1;last_service=~0u;
    statistics.available=2; /* Transport compiled in; does not assert PC connected. */
}
void usurper_audio_configure(const usurper_settings_t *v){
    if(us_settings_valid(v)){preferences=*v;dirty=1;}
}
void usurper_audio_event(unsigned id){
    /* Original minimum intervals rounded up to 60 Hz ticks. */
    static const uint8_t interval[]={0,3,4,3,5,14,4,4,3,1,1,18,3,5,5,6};
    if(!id||id>US_AUDIO_RAIL)return;
    ++statistics.requested;
    if(muted||!(preferences.flags&US_SETTINGS_SFX)||!preferences.sfx_volume||
       ((seen&(1u<<id))&&tick-last[id]<interval[id])){++statistics.suppressed;return;}
    seen|=1u<<id;last[id]=tick;
    if(count==8u){++statistics.software_dropped;return;}
    events[count++]=(uint8_t)id;++statistics.queued;
}
void usurper_audio_tick(unsigned value){
    ++tick;
    if(value!=phase){phase=value;dirty=1;}
}
void usurper_audio_service(void){
    uint8_t bytes[16];char line[40];unsigned size=7u,checksum=0;
    if(last_service==tick&&!dirty)return;
    if(!dirty&&!count&&tick-last_state<60u)return;
    bytes[0]=(uint8_t)(sequence>>8);bytes[1]=(uint8_t)sequence;
    bytes[2]=(uint8_t)phase;bytes[3]=(uint8_t)preferences.flags;
    bytes[4]=(uint8_t)preferences.music_volume;bytes[5]=(uint8_t)preferences.sfx_volume;bytes[6]=(uint8_t)muted;
    for(unsigned i=0;i<count;++i)bytes[size++]=events[i];
    memcpy(line,"UA1=",4);
    for(unsigned i=0;i<size;++i){checksum^=bytes[i];hex_byte(line+4u+i*2u,bytes[i]);}
    hex_byte(line+4u+size*2u,checksum);line[6u+size*2u]='\n';
    if(uart_try_write(line,7u+size*2u)){
        statistics.submitted+=count;++sequence;dirty=0;last_state=tick;
    }else{statistics.software_dropped+=count;}
    count=0;last_service=tick;
}
void usurper_audio_mute(unsigned value){
    muted=!!value;count=0;dirty=1;usurper_audio_service();
}
const usurper_audio_stats_t *usurper_audio_stats(void){return &statistics;}
