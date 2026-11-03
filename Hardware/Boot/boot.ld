OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0xf9000000, LENGTH = 4K }
SECTIONS {
 .text : { KEEP(*(.init)) *(.text*) } > ram
 .rodata : { *(.rodata*) *(.srodata*) } > ram
 .data : { *(.data*) *(.sdata*) } > ram
 .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
 __stack_top = 0xf9001000;
 ASSERT(__bss_end <= 0xf9000c00, "Boot exceeds 3 KiB")
}
