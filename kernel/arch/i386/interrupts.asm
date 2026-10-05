; Interrupt entry stubs. Each stub pushes a dummy error code when the CPU does not push one,
; then its vector number, and jumps to the common stub, which saves the registers, switches
; to kernel data segments and calls interrupt_dispatch(registers_t *).

extern interrupt_dispatch

%macro STUB_NOERR 1
stub_%1:
    push dword 0
    push dword %1
    jmp common_stub
%endmacro

%macro STUB_ERR 1
stub_%1:
    push dword %1
    jmp common_stub
%endmacro

section .text
common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp                    ; registers_t *
    call interrupt_dispatch
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8                  ; vector number and error code
    iret

; CPU exceptions 0-31: 8, 10-14, 17, 21, 29, 30 push an error code.
%assign v 0
%rep 32
  %if v == 8 || (v >= 10 && v <= 14) || v == 17 || v == 21 || v == 29 || v == 30
    STUB_ERR %[v]
  %else
    STUB_NOERR %[v]
  %endif
  %assign v v + 1
%endrep

; Hardware IRQs 0-15 remapped to vectors 32-47, and the system call vector.
%assign v 32
%rep 16
    STUB_NOERR %[v]
  %assign v v + 1
%endrep
STUB_NOERR 128

section .rodata
global interrupt_stub_table
interrupt_stub_table:           ; addresses of stubs for vectors 0-47
%assign v 0
%rep 48
    dd stub_%[v]
  %assign v v + 1
%endrep
global syscall_stub
syscall_stub:
    dd stub_128

section .note.GNU-stack noalloc noexec nowrite progbits
