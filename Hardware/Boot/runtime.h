#ifndef GRAVITY_RUNTIME_H
#define GRAVITY_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define UART(o) REG32(0xf8010000u+(o))
#define GPU(o) REG32(0xf8100000u+(o))
#define CPU_HZ 100000000u
static inline uint32_t now(void) {return REG32(0xf8b0bff8u);}
static inline uint64_t now64(void) {
    uint32_t hi,lo,again;
    do{hi=REG32(0xf8b0bffcu);lo=now();again=REG32(0xf8b0bffcu);}while(hi!=again);
    return ((uint64_t)hi<<32)|lo;
}
static inline void barrier(void) {__asm__ volatile("fence iorw, iorw" ::: "memory");}
/* This generated Sapphire uses a write-through DataCache. CSR 0x810 appears
 * in the generic BSP but is NOT decoded by this CPU; never execute it here.
 * Fence orders the stores, and the supported 0x500f instruction invalidates
 * read cache lines before ownership moves between CPU and GPU. The UART boot
 * loader additionally checks the complete DDR image after invalidation.
 */
static inline void drain(void) {
    barrier();
    __asm__ volatile(".word 0x500f" ::: "memory");
    barrier();
}
static inline void invalidate(void) {__asm__ volatile(".word 0x500f" ::: "memory");barrier();}
static inline void uart_init(void) {UART(8)=CPU_HZ/(115200u*8u)-1;UART(12)=7;}
#ifdef GRAVITY_ASYNC_UART
void putch(char ch);
void uart_service(void);
void uart_flush(void);
#else
static inline void putch(char ch) {while(!((UART(4)>>16)&255u)){}UART(0)=(uint8_t)ch;}
static inline void uart_service(void) {}
static inline void uart_flush(void) {}
#endif
static inline void puts_uart(const char *s) {while(*s)putch(*s++);}
static inline int getch(void) {if(!(UART(4)>>24))return -1;return UART(0)&255;}
void number(uint32_t n);
void trap_report(uint32_t cause,uint32_t pc,uint32_t value);
#endif
