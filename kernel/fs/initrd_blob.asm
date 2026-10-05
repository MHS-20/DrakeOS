; The initrd image, linked into the kernel's read-only data (built by tools/mkinitrd).
section .rodata
align 4
global initrd_start, initrd_end
initrd_start:
    incbin "initrd.img"
initrd_end:

section .note.GNU-stack noalloc noexec nowrite progbits
