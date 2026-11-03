/* Volatile JTAG boot ROM, checked UART load into DDR; never writes flash. */
#include "runtime.h"
static int byte(void) {
 uint32_t start=now();int b;
 do {b=getch();if((uint32_t)(now()-start)>CPU_HZ*3u)return -1;}while(b<0);
 return b;
}
static uint32_t word(void) {uint32_t v=0;for(unsigned i=0;i<4;i++){int b=byte();if(b<0)return 0;v|=(uint32_t)b<<(i*8);}return v;}
int main(void) {
 uart_init();puts_uart("STARFIELD_BOOT waiting DDR\r\n");
 uint32_t start=now();
 while(!GPU(0x420))if((uint32_t)(now()-start)>CPU_HZ*10u){puts_uart("FAIL DDR calibration\r\n");return 1;}
 for(;;) {
  puts_uart("READY TS2D MAX_BYTES=917504\r\n");
  if(byte()!='T' || byte()!='S' || byte()!='2' || byte()!='D')continue;
  uint32_t size=word(),expected=word();
  /* Match starfield.ld: payload starts at 0x10000, stack is reserved from
   * 0xf0000. Linked .bss is checked independently by the linker. */
  if(!size || size>896u*1024u){puts_uart("FAIL load size\r\n");continue;}
  volatile uint8_t *dest=(volatile uint8_t *)0x10000;
  uint32_t crc=0xffffffffu;unsigned i;
  for(i=0;i<size;i++) {
   int b=byte();if(b<0)break;dest[i]=(uint8_t)b;crc^=(uint8_t)b;
   for(unsigned k=0;k<8;k++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
  }
  if(i!=size || (crc^0xffffffffu)!=expected){puts_uart("FAIL load CRC\r\n");continue;}
  drain();invalidate();
  crc=0xffffffffu;
  for(i=0;i<size;i++){crc^=dest[i];for(unsigned k=0;k<8;k++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
  if((crc^0xffffffffu)!=expected){puts_uart("FAIL DDR readback CRC\r\n");continue;}
  puts_uart("DDR_LOAD=PASS\r\n");
  __asm__ volatile("fence.i" ::: "memory");
  ((void (*)(void))0x10000)();
 }
}
