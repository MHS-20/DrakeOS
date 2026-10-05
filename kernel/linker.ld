/*
 * Higher-half kernel: loaded at physical 1 MB, linked at 0xC0100000 (+ the size of .boot).
 * The .boot section runs before paging and is linked at its physical address.
 */
ENTRY(_start)

KERNEL_VMA = 0xC0000000;

SECTIONS
{
    . = 0x00100000;
    kernel_phys_start = .;

    .boot :
    {
        *(.boot.entry)
        *(.multiboot)
        *(.boot.text)
        *(.boot.data)
    }

    . += KERNEL_VMA;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(16);
        bss_end = .;
    }

    . = ALIGN(4K);
    kernel_phys_end = . - KERNEL_VMA;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
