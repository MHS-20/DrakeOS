; Kernel entry point, shared by GRUB (Multiboot) and the DrakeOS stage2 loader.
;
; Both loaders enter at physical 0x00100000 in 32-bit protected mode with flat segments,
; paging off, EAX = 0x2BADB002 and EBX = physical address of a multiboot_info structure.
; This code runs before paging, so it lives in the low (identity) .boot section and only
; touches higher-half symbols through their physical addresses (symbol - KERNEL_VMA).

KERNEL_VMA      equ 0xC0000000
KERNEL_PDE      equ (KERNEL_VMA >> 22)          ; 768
MB_MAGIC        equ 0x1BADB002
MB_FLAGS        equ (1 << 0) | (1 << 1)         ; page-align modules, provide memory info
PDE_4M          equ 0x83                        ; present | writable | 4 MB page
BOOT_MAP_PAGES  equ 4                           ; 16 MB mapped twice during boot
STACK_SIZE      equ 16384

extern kmain
extern bss_start, bss_end

; The stage2 loader jumps to 0x00100000, so the very first bytes must be code.
section .boot.entry exec
global _start
_start:
    jmp short boot_start

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)

section .boot.text exec
boot_start:
    cli
    cld
    mov esi, eax                        ; keep magic and info pointer across setup
    mov ebp, ebx

    ; Zero .bss (GRUB does it, the stage2 loader does not).
    mov edi, bss_start - KERNEL_VMA
    mov ecx, bss_end - KERNEL_VMA
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    ; Enable 4 MB pages, load the boot page directory, turn on paging (PG) and write-protect (WP).
    mov eax, cr4
    or eax, 0x10
    mov cr4, eax
    mov eax, boot_page_directory
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80010000
    mov cr0, eax

    ; Absolute jump: from here on eip is in the higher half.
    mov eax, higher_half
    jmp eax

section .boot.data write align=4096
; Identity-maps and higher-half-maps the first 16 MB with 4 MB pages. vmm_init() replaces it.
boot_page_directory:
%assign i 0
%rep 1024
  %if i < BOOT_MAP_PAGES
    dd (i << 22) | PDE_4M
  %elif i >= KERNEL_PDE && i < KERNEL_PDE + BOOT_MAP_PAGES
    dd ((i - KERNEL_PDE) << 22) | PDE_4M
  %else
    dd 0
  %endif
  %assign i i + 1
%endrep

section .text
higher_half:
    mov esp, stack_top                  ; 16-byte aligned (see .bss)
    sub esp, 8                          ; keep esp 16-aligned at the call
    push ebp                            ; kmain(magic, physical multiboot info pointer)
    push esi
    xor ebp, ebp                        ; terminates frame-pointer backtraces
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb STACK_SIZE
global stack_top
stack_top:
