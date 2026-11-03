/* Runs from DDR under the old 512 KiB loader. The generated Sapphire's
 * 4 KiB boot RAM is writable; replace only its code, verify readback, then
 * restart the new loader. JTAG reconfiguration restores the original RAM.
 * Interrupts and the stack remain in DDR while the boot RAM is overwritten. */
#include "runtime.h"
#include "usurper_boot_upgrade_image.h"

int main(void) {
    uart_init();
    volatile uint8_t *dest=(volatile uint8_t *)(uintptr_t)0xf9000000u;
    for(unsigned i=0;i<USURPER_BOOT_IMAGE_BYTES;++i)dest[i]=usurper_boot_image[i];
    drain();invalidate();
    uint32_t crc=0xffffffffu;
    for(unsigned i=0;i<USURPER_BOOT_IMAGE_BYTES;++i){
        crc^=dest[i];
        for(unsigned k=0;k<8;++k)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
    }
    if((crc^0xffffffffu)!=USURPER_BOOT_IMAGE_CRC){
        puts_uart("FAIL BOOT_UPGRADE readback CRC\r\n");return 1;
    }
    puts_uart("BOOT_UPGRADE=PASS\r\n");
    __asm__ volatile("fence.i" ::: "memory");
    ((void (*)(void))(uintptr_t)0xf9000000u)();
    return 0;
}
