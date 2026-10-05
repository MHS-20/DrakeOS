; Kernel context switching and the first entry into new threads and user processes.

section .text
global switch_context, kthread_entry, user_entry
extern process_exit

; void switch_context(uint32_t *old_esp, uint32_t new_esp)
; Saves the callee-saved registers on the current kernel stack, stores esp in *old_esp,
; then resumes the context whose stack pointer is new_esp. Call with interrupts disabled.
switch_context:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; First return target of a new kernel thread: EBX = function, ESI = argument.
kthread_entry:
    sti
    push esi
    call ebx
    add esp, 4
    push eax
    call process_exit           ; never returns

; First return target of a new user process: the stack holds an iret frame for ring 3.
user_entry:
    mov ax, 0x23                ; user data selector, RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
