; User program entry: the kernel leaves argc at [esp] and argv at [esp + 4].
section .text
global _start
extern main, exit

_start:
    mov eax, [esp]              ; argc
    mov edx, [esp + 4]          ; argv
    and esp, -16
    sub esp, 8
    push edx
    push eax
    call main
    push eax
    call exit                   ; never returns

section .note.GNU-stack noalloc noexec nowrite progbits
