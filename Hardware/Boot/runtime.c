#include "runtime.h"
#ifdef GRAVITY_ASYNC_UART
static char tx[2048];static unsigned tx_read,tx_write;
void uart_service(void) {
 unsigned available=(UART(4)>>16)&255u;
 while(available && tx_read!=tx_write) {
  UART(0)=(uint8_t)tx[tx_read];tx_read=(tx_read+1)&2047u;available--;
 }
}
void putch(char ch) {
 unsigned next=(tx_write+1)&2047u;
 while(next==tx_read)uart_service();
 tx[tx_write]=ch;tx_write=next;
}
void uart_flush(void) {while(tx_read!=tx_write)uart_service();}
#endif
typedef uint32_t alias_word __attribute__((__may_alias__));
void *memset(void *dest,int value,size_t n) {
 unsigned char *d=dest;uint32_t word=(uint8_t)value*0x01010101u;
 while(n && ((uintptr_t)d&3)){*d++=(unsigned char)value;n--;}
 while(n>=4){*(alias_word *)d=word;d+=4;n-=4;}
 while(n--)*d++=(unsigned char)value;
 return dest;
}
void *memcpy(void *dest,const void *src,size_t n) {
 unsigned char *d=dest;const unsigned char *s=src;
 if(((uintptr_t)d&3)==((uintptr_t)s&3)) {
  while(n && ((uintptr_t)d&3)){*d++=*s++;n--;}
  while(n>=4){*(alias_word *)d=*(const alias_word *)s;d+=4;s+=4;n-=4;}
 }
 while(n--)*d++=*s++;
 return dest;
}
int strcmp(const char *left,const char *right) {
 const unsigned char *a=(const unsigned char *)left,*b=(const unsigned char *)right;
 while(*a && *a==*b){a++;b++;}
 return (int)*a-(int)*b;
}
void number(uint32_t n) {char b[11];unsigned k=10;b[k]=0;do{b[--k]=(char)('0'+n%10);n/=10;}while(n);puts_uart(b+k);}
void trap_report(uint32_t cause,uint32_t pc,uint32_t value) {
 puts_uart("TRAP cause=");number(cause);puts_uart(" pc=");number(pc);puts_uart(" value=");number(value);puts_uart("\r\n");for(;;){uart_service();}
}
