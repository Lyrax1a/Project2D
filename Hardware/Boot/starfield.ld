OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x00010000, LENGTH = 960K }
SECTIONS {
 .text : { KEEP(*(.init)) *(.text*) } > ram
 .rodata : { *(.rodata*) *(.srodata*) } > ram
 .data : { *(.data*) *(.sdata*) } > ram
 .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
 __stack_top = 0x00100000;
 ASSERT(__bss_end <= 0x000f0000, "Firmware overlaps stack")
}
