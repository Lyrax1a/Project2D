#include "runtime.h"
#include "gpu_runtime.h"

// Addresses and source 16 are from this board's generated Sapphire BSP.
#define PLIC(o) REG32(0xf8c00000u+(o))
volatile uint32_t gpu_irq_count,gpu_timer_wakes;
uint64_t gpu_wait_cycles;
static volatile unsigned pending;
static void (*background_work)(void);
static unsigned poll_wait;
static void timer_compare(uint64_t deadline) {
    REG32(0xf8b04004u)=~0u;
    REG32(0xf8b04000u)=(uint32_t)deadline;
    REG32(0xf8b04004u)=(uint32_t)(deadline>>32);barrier();
}
void interrupt_dispatch(uint32_t cause,uint32_t pc,uint32_t value) {
    if(cause==0x8000000bu) {
        unsigned claim;
        while((claim=PLIC(0x200004))!=0) {
            if(claim!=16)trap_report(cause,pc,claim);
            GPU(16)=1;barrier();gpu_irq_count++;pending=0;
            PLIC(0x200004)=claim;barrier();
        }
    } else if(cause==0x80000007u) {
        timer_compare(~(uint64_t)0);gpu_timer_wakes++;
    } else trap_report(cause,pc,value);
}
void gpu_interrupt_init(void) {
    timer_compare(~(uint64_t)0);GPU(16)=1;barrier();
    PLIC(16*4)=1;PLIC(0x200000)=0;PLIC(0x2000)=1u<<16;barrier();
    __asm__ volatile("csrs mie, %0" :: "r"(0x880u):"memory");
    __asm__ volatile("csrsi mstatus, 8" ::: "memory");
}
void gpu_wait_policy(unsigned polling){poll_wait=polling;}
void gpu_submission_begin(void){pending=1;barrier();}
unsigned gpu_submission_pending(void){return pending;}
void gpu_set_background_work(void (*work)(void)){background_work=work;}
int gpu_wait_complete(void) {
    if(!pending && !(GPU(12)&2))return GPU(20)?-1:0;
    uint64_t start=now64();
    uint64_t deadline=now64()+(uint64_t)CPU_HZ*5;
    if(poll_wait) {
        while(GPU(12)&2){uart_service();if(now64()>=deadline)return -1;}
    } else {
        timer_compare(deadline);
        // This generated Sapphire treats WFI as a hint, not a pipeline stall.
        // Completion is an ISR-owned software flag; callers can do useful work
        // before joining, or supply a non-submitting background callback here.
        while(pending) {
            if(background_work)background_work();
            if(now64()>=deadline){timer_compare(~(uint64_t)0);return -1;}
            uart_service();
        }
        timer_compare(~(uint64_t)0);
    }
    gpu_wait_cycles+=now64()-start;
    return GPU(20)?-1:0;
}
