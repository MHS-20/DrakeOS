; Loading descriptor tables. CS can only be reloaded with a far jump.
section .text
global gdt_flush, tss_flush, idt_flush

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs
.reload_cs:
    ret

tss_flush:
    mov ax, [esp + 4]
    ltr ax
    ret

idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
