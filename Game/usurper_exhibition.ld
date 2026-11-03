OUTPUT_ARCH(riscv)
ENTRY(_start)
/* Checked UART image: 0x10000..0xf0000. The unchanged 64 KiB stack ends
 * at0x100000; zero-initialized game state has an exclusive DDR allocation. */
MEMORY {
 image (rwx) : ORIGIN = 0x00010000, LENGTH = 896K
 state (rw) : ORIGIN = 0x00100000, LENGTH = 1M
}
SECTIONS {
 .text : { KEEP(*(.init)) *(.text*) } > image
 .rodata : { *(.rodata*) *(.srodata*) } > image
 .data : { *(.data*) *(.sdata*) . = ALIGN(4); __image_end = .; } > image
 .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > state
 __stack_top = 0x00100000;
 ASSERT(__image_end <= 0x000f0000, "Firmware exceeds UART image/stack boundary")
 ASSERT(__bss_start >= __stack_top && __bss_end <= 0x00200000, "Game state outside exclusive DDR allocation")
}
