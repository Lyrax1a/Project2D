/* SPDX-License-Identifier: MIT
 * Bounded event queue for the HDMI hardware synthesizer. "submitted" counts
 * accepted commands, never proof that a physical sink played a sound. */
#define USURPER_AUDIO_IMPLEMENTATION
#include "usurper_audio.h"
#include <string.h>
#ifdef USURPER_AUDIO_PC
#include "usurper_audio_pc_impl.h"
#else
#ifdef USURPER_AUDIO_HOST
extern uint32_t us_audio_read(unsigned offset);
extern void us_audio_write(unsigned offset,uint32_t value);
#else
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
static uint32_t us_audio_read(unsigned offset){return GPU(0x200u+offset);}
static void us_audio_write(unsigned offset,uint32_t value){GPU(0x200u+offset)=value;}
#endif
static usurper_audio_stats_t statistics;
static usurper_settings_t preferences=US_SETTINGS_DEFAULT;
static uint16_t queue[32];
static unsigned head,tail,count,tick,muted;
static unsigned last[32],seen;
static void submit(unsigned id,unsigned gain){
    if(!gain||muted){++statistics.suppressed;return;}
    if(count==32){++statistics.software_dropped;return;}
    queue[tail]=(uint16_t)(id|(gain<<8));tail=(tail+1)&31u;++count;++statistics.queued;
}
void usurper_audio_init(void){
    memset(&statistics,0,sizeof(statistics));memset(last,0,sizeof(last));
    head=tail=count=tick=muted=seen=0;
    statistics.available=us_audio_read(0)==0x41554431u;
    if(statistics.available)us_audio_write(12,2u); /* clear voices/queue */
}
void usurper_audio_configure(const usurper_settings_t *v){if(us_settings_valid(v))preferences=*v;}
void usurper_audio_mute(unsigned value){muted=!!value;head=tail=count=0;if(statistics.available)us_audio_write(12,muted?3u:2u);}
void usurper_audio_event(unsigned id){
    static const uint8_t interval[]={0,3,4,2,5,14,4,4,3,1,1,18};
    if(!id||id>US_AUDIO_WARNING)return;
    ++statistics.requested;
    if(!(preferences.flags&US_SETTINGS_SFX)){++statistics.suppressed;return;}
    if((seen&(1u<<id))&&tick-last[id]<interval[id]){++statistics.suppressed;return;}
    seen|=1u<<id;last[id]=tick;
    submit(id,preferences.sfx_volume*204u/100u);
}
void usurper_audio_tick(unsigned phase){
    /* A quiet, original procedural eight-note accompaniment. It is not the
     * Godot soundtrack and is documented as a board audio adaptation. */
    static const uint8_t notes[]={0,2,4,2,1,3,5,3,0,2,6,4,1,3,7,5};
    ++tick;
    if(!(tick%24u)&&(preferences.flags&US_SETTINGS_MUSIC)&&phase!=3u&&phase!=9u){
        unsigned gain=preferences.music_volume*51u/100u;
        submit(US_AUDIO_MUSIC_FIRST+notes[(tick/24u)&15u],gain);
    }
}
void usurper_audio_service(void){
    if(!statistics.available){head=tail=count=0;return;}
    /* Never wait for a FIFO slot. Eight writes is a strict per-service bound. */
    for(unsigned i=0;i<8u&&count;++i){
        if(!(us_audio_read(4)&1u))break;
        us_audio_write(8,queue[head]);head=(head+1)&31u;--count;++statistics.submitted;
    }
    statistics.hardware_dropped=us_audio_read(16);statistics.samples=us_audio_read(20);
}
const usurper_audio_stats_t *usurper_audio_stats(void){return &statistics;}
#endif
