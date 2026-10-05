/* User programs: static ELF executables at the traditional i386 address. */
ENTRY(_start)

SECTIONS
{
    . = 0x08048000;
    .text : { *(.text .text.*) }
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }
    .data ALIGN(4K) : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) }
}
