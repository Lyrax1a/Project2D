/* SPDX-License-Identifier: MIT */
#include "usurper_profile_console.h"
#include "usurper_scene.h"
#include <string.h>
#ifndef US_PROFILE_CONSOLE_NO_UART
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
#endif

static us_profile_console_state_t console_state;
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void write32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;++i){p[i]=(uint8_t)x;x>>=8;}}
unsigned usurper_profile_active_slot(void){return console_state.initialized?console_state.active_slot:US_PROFILE_SLOT_NORMAL;}

int us_profile_console_enter(us_profile_console_state_t *state) {
    if(!state)return US_PROFILE_ARGUMENT;
    us_profile_t snapshot;
    int status=usurper_scene_profile_export(&snapshot);
    if(status)return status;
    us_profile_console_state_t next=*state;
    if(!next.initialized){
        memset(&next,0,sizeof(next));
        usurper_scene_profile_rules(&next.rules);
        next.active_slot=US_PROFILE_SLOT_NORMAL;
    }
    status=us_profile_store_save(&next.store,next.active_slot,&snapshot,&next.rules);
    if(status)return status;
    next.initialized=1;*state=next;return US_PROFILE_OK;
}

static int process(us_profile_console_state_t *state,const uint8_t *request,
                   const void *payload,uint32_t payload_bytes,uint8_t *output,uint32_t *output_bytes) {
    if(read32(request)!=US_PROFILE_REQUEST_MAGIC||read32(request+24)||
       read32(request+28)!=us_profile_crc32(request,28))return US_PROFILE_FORMAT;
    uint32_t operation=read32(request+4),slot=read32(request+8),size=read32(request+16);
    if(operation>US_PROFILE_INFO)return US_PROFILE_UNSUPPORTED;
    if(slot>=US_PROFILE_SLOT_COUNT)return US_PROFILE_SLOT;
    if(size!=payload_bytes || (operation==US_PROFILE_PUT?size!=US_PROFILE_BYTES:size!=0u))return US_PROFILE_FORMAT;
    if(payload_bytes&&!payload)return US_PROFILE_ARGUMENT;
    if(!state->initialized)return US_PROFILE_EMPTY;
    if(read32(request+20)!=state->rules.catalog_hash)return US_PROFILE_RULES;
    /* Every request rechecks settled-state permission; neither malformed nor
     * busy commands can touch a journal or change the active slot. */
    us_profile_t current,target;
    int status=usurper_scene_profile_export(&current);
    if(status)return status;
    if(operation==US_PROFILE_EXIT)return US_PROFILE_OK;
    us_profile_store_t temporary=state->store;
    unsigned new_active=state->active_slot;
    if(operation==US_PROFILE_INFO)slot=state->active_slot;
    if(operation==US_PROFILE_GET||operation==US_PROFILE_INFO) {
        if(slot==state->active_slot){
            status=us_profile_store_save(&temporary,slot,&current,&state->rules);
            if(status)return status;
        }
        status=us_profile_store_export(&temporary,slot,&state->rules,output);
        if(status)return status;
    } else if(operation==US_PROFILE_SWITCH) {
        status=us_profile_store_save(&temporary,state->active_slot,&current,&state->rules);
        if(status)return status;
        status=us_profile_store_load(&temporary,slot,&state->rules,&target,0);
        if(status==US_PROFILE_EMPTY){
            status=us_profile_store_reset(&temporary,slot,&state->rules,US_PROFILE_PROGRESSION|US_PROFILE_LOADOUT|US_PROFILE_SETTINGS);
            if(!status)status=us_profile_store_load(&temporary,slot,&state->rules,&target,0);
        }
        if(status)return status;
        status=us_profile_store_export(&temporary,slot,&state->rules,output);
        if(status)return status;
        if(slot!=state->active_slot){status=usurper_scene_profile_import(&target);if(status)return status;}
        new_active=slot;
    } else {
        if(operation==US_PROFILE_PUT){
            status=us_profile_decode(payload,payload_bytes,slot,&state->rules,&target,0);
            if(status)return status;
        } else us_profile_defaults(&target,US_PROFILE_PROGRESSION|US_PROFILE_LOADOUT|US_PROFILE_SETTINGS);
        if(slot==state->active_slot){
            status=us_profile_store_save(&temporary,slot,&current,&state->rules);
            if(status)return status;
        }
        status=us_profile_store_save(&temporary,slot,&target,&state->rules);
        if(status)return status;
        status=us_profile_store_export(&temporary,slot,&state->rules,output);
        if(status)return status;
        if(slot==state->active_slot){status=usurper_scene_profile_import(&target);if(status)return status;}
    }
    state->store=temporary;state->active_slot=new_active;*output_bytes=US_PROFILE_BYTES;
    return US_PROFILE_OK;
}

int us_profile_console_handle(us_profile_console_state_t *state,const uint8_t request[US_PROFILE_PACKET_BYTES],
                              const void *payload,uint32_t payload_bytes,uint8_t response[US_PROFILE_PACKET_BYTES],
                              uint8_t output[US_PROFILE_BYTES],uint32_t *output_bytes) {
    if(!state||!request||!response||!output||!output_bytes)return US_PROFILE_ARGUMENT;
    uint8_t result[US_PROFILE_BYTES];uint32_t bytes=0;
    int status=process(state,request,payload,payload_bytes,result,&bytes);
    write32(response,US_PROFILE_RESPONSE_MAGIC);write32(response+4,read32(request+4));
    write32(response+8,read32(request+8));write32(response+12,read32(request+12));
    write32(response+16,(uint32_t)status);write32(response+20,state->active_slot);
    write32(response+24,status?0u:bytes);write32(response+28,us_profile_crc32(response,28));
    *output_bytes=status?0u:bytes;
    if(!status&&bytes)memcpy(output,result,bytes);
    return status;
}

#ifndef US_PROFILE_CONSOLE_NO_UART
static int read_exact(uint8_t *data,unsigned bytes,unsigned seconds) {
    uint64_t deadline=now64()+(uint64_t)CPU_HZ*seconds;
    unsigned used=0;
    while(used<bytes&&now64()<deadline){int c=getch();uart_service();if(c>=0)data[used++]=(uint8_t)c;}
    return used==bytes?0:-1;
}
static void emit_bytes(const uint8_t *data,unsigned bytes){for(unsigned i=0;i<bytes;++i)putch((char)data[i]);}
static void ready(void){
    puts_uart("UP_READY v=1 active=");number(console_state.active_slot);
    puts_uart(" catalog=");number(console_state.rules.catalog_hash);
    puts_uart(" bytes=256 volatile=1\r\n");uart_flush();
}
static void abort_input(unsigned code){
    puts_uart("UP_ABORT code=");number(code);puts_uart("\r\n");uart_flush();
    /* Bounded discard reduces late bytes leaking back into normal controls. */
    uint64_t end=now64()+CPU_HZ/2;
    while(now64()<end){(void)getch();uart_service();}
}
void usurper_profile_console(void) {
    int status=us_profile_console_enter(&console_state);
    if(status){puts_uart("UP_BUSY code=");number((uint32_t)-status);puts_uart("\r\n");uart_flush();return;}
    for(;;){
        ready();uint8_t request[32],payload[256],response[32],output[256];uint32_t bytes=0;
        if(read_exact(request,1,60)){puts_uart("UP_IDLE_TIMEOUT\r\n");break;}
        if(read_exact(request+1,31,5)){abort_input(1);break;}
        if(read32(request)!=US_PROFILE_REQUEST_MAGIC||read32(request+24)||
           read32(request+28)!=us_profile_crc32(request,28)) {abort_input(2);break;}
        uint32_t payload_bytes=read32(request+16);
        if(payload_bytes>256u||(payload_bytes&&payload_bytes!=256u)){abort_input(3);break;}
        if(payload_bytes&&read_exact(payload,payload_bytes,5)){abort_input(4);break;}
        status=us_profile_console_handle(&console_state,request,payload,payload_bytes,response,output,&bytes);
        emit_bytes(response,sizeof(response));if(bytes)emit_bytes(output,bytes);uart_flush();
        if(!status&&read32(request+4)==US_PROFILE_EXIT)break;
    }
    puts_uart("UP_EXIT\r\n");uart_flush();
}
#endif
