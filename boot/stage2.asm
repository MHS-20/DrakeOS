; DrakeOS stage 2: everything a 512-byte boot sector cannot fit.
;
;  1. enable the A20 line
;  2. collect the BIOS memory map (int 0x15, eax = 0xE820) and memory sizes
;  3. read the kernel image from disk into a low buffer (0x10000, up to 512 KB)
;  4. switch to 32-bit protected mode with a flat GDT
;  5. copy the kernel to 1 MB and enter it exactly like a Multiboot loader:
;     EAX = 0x2BADB002, EBX = physical address of a multiboot_info structure.

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif
%ifndef KERNEL_SECTORS
%error "KERNEL_SECTORS must be defined by the build"
%endif

KERNEL_LBA      equ 1 + STAGE2_SECTORS
BUFFER_SEG      equ 0x1000              ; kernel staging buffer at 0x10000
KERNEL_PHYS     equ 0x00100000
MBI_ADDR        equ 0x6000              ; multiboot_info_t
MMAP_ADDR       equ 0x6100              ; multiboot mmap entries (24 bytes each)
MMAP_MAX        equ 32
MB_MAGIC        equ 0x2BADB002
MB_FLAGS        equ (1 << 0) | (1 << 6) | (1 << 9)   ; mem_lower/upper, mmap, loader name

[org 0x8000]
[bits 16]
stage2:
    mov [boot_drive], dl
    mov si, msg_stage2
    call print

    call enable_a20
    call detect_memory
    call load_kernel

    mov si, msg_pm
    call print
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:protected_mode         ; far jump loads CS and flushes the prefetch queue

; ------------------------------------------------------------------ A20
; With A20 masked, bit 20 of every address is forced to 0 and memory above 1 MB aliases
; the first megabyte. QEMU enables it already; real machines may not.
enable_a20:
    call a20_enabled
    jnz .done
    mov ax, 0x2401                      ; BIOS: enable A20
    int 0x15
    call a20_enabled
    jnz .done
    in al, 0x92                         ; "fast A20" through system control port A
    or al, 2
    and al, 0xfe                        ; never set bit 0: it resets the machine
    out 0x92, al
    call a20_enabled
    jnz .done
    mov si, msg_a20
    call print
    jmp halt
.done:
    ret

; ZF clear if A20 is enabled: compares 0000:0500 with its 1 MB alias FFFF:0510.
a20_enabled:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    not ax
    mov es, ax
    mov al, [ds:0x0500]
    push ax
    mov byte [ds:0x0500], 0x00
    mov byte [es:0x0510], 0xff
    cmp byte [ds:0x0500], 0xff
    pop ax
    mov [ds:0x0500], al
    pop es
    pop ds
    ret                                 ; ZF set = wrapped around = A20 disabled

; ------------------------------------------------------------------ memory map
detect_memory:
    ; Fill in the multiboot_info structure the kernel expects.
    mov di, MBI_ADDR
    mov cx, 88 / 2
    xor ax, ax
    rep stosw
    mov dword [MBI_ADDR + 0], MB_FLAGS
    mov dword [MBI_ADDR + 64], loader_name

    int 0x12                            ; AX = KB of conventional memory
    mov [MBI_ADDR + 4], ax

    ; int 0x15, ax = 0xE801: AX/CX = KB between 1 and 16 MB, BX/DX = 64 KB blocks above 16 MB
    mov ax, 0xe801
    int 0x15
    jc .no_e801
    test ax, ax
    jnz .e801_ok
    mov ax, cx
    mov bx, dx
.e801_ok:
    movzx eax, ax
    movzx ebx, bx
    shl ebx, 6
    add eax, ebx
    mov [MBI_ADDR + 8], eax
.no_e801:

    ; int 0x15, eax = 0xE820, repeated until EBX = 0. Each multiboot entry is
    ; { u32 size = 20; u64 base; u64 length; u32 type }, so the BIOS writes at entry + 4.
    xor ebx, ebx
    xor bp, bp                          ; entry count
    mov di, MMAP_ADDR + 4
.e820:
    mov eax, 0xe820
    mov edx, 0x534d4150                 ; 'SMAP'
    mov ecx, 20
    int 0x15
    jc .e820_done
    cmp eax, 0x534d4150
    jne .e820_done
    mov dword [di - 4], 20
    add di, 24
    inc bp
    cmp bp, MMAP_MAX
    je .e820_done
    test ebx, ebx
    jnz .e820
.e820_done:
    movzx eax, bp
    imul eax, 24
    mov [MBI_ADDR + 44], eax            ; mmap_length
    mov dword [MBI_ADDR + 48], MMAP_ADDR
    ret

; ------------------------------------------------------------------ kernel loading
; Reads KERNEL_SECTORS sectors starting at KERNEL_LBA into BUFFER_SEG:0, one sector at a
; time so no read crosses a track or a 64 KB DMA boundary. Uses the BIOS extended read
; (int 0x13, ah = 0x42) when available, else CHS with the geometry from ah = 0x08.
load_kernel:
    mov si, msg_loading
    call print

    mov ah, 0x41                        ; extensions installed?
    mov bx, 0x55aa
    mov dl, [boot_drive]
    int 0x13
    jc .chs_geometry
    cmp bx, 0xaa55
    jne .chs_geometry
    test cx, 1                          ; packet interface supported
    jz .chs_geometry
    mov byte [use_lba], 1
    jmp .start

.chs_geometry:
    mov ah, 0x08
    mov dl, [boot_drive]
    xor di, di
    mov es, di
    int 0x13
    jc disk_error
    and cx, 0x3f                        ; sectors per track (bits 0-5 of CL)
    mov [spt], cx
    movzx dx, dh
    inc dx                              ; heads = max head + 1
    mov [heads], dx

.start:
    mov dword [lba], KERNEL_LBA
    mov word [buf_seg], BUFFER_SEG
    mov word [remaining], KERNEL_SECTORS
.next_sector:
    cmp word [remaining], 0
    je .done
    call read_sector
    add word [buf_seg], 512 / 16
    inc dword [lba]
    dec word [remaining]
    test word [remaining], 63
    jnz .next_sector
    mov si, msg_dot
    call print
    jmp .next_sector
.done:
    mov si, msg_ok
    call print
    ret

read_sector:
    mov byte [retries], 3
.retry:
    cmp byte [use_lba], 0
    je .chs
    mov si, dap
    mov ax, [buf_seg]
    mov [dap.segment], ax
    mov eax, [lba]
    mov [dap.lba], eax
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .ok
    jmp .failed
.chs:
    ; sector = lba % spt + 1, head = (lba / spt) % heads, cylinder = lba / spt / heads
    mov eax, [lba]
    xor edx, edx
    movzx ecx, word [spt]
    div ecx
    inc dl
    mov cl, dl                          ; sector (bits 0-5)
    xor edx, edx
    movzx ebx, word [heads]
    div ebx                             ; eax = cylinder, edx = head
    mov dh, dl
    mov ch, al                          ; cylinder bits 0-7
    shl ah, 6
    or cl, ah                           ; cylinder bits 8-9 into CL bits 6-7
    mov dl, [boot_drive]
    mov ax, [buf_seg]
    mov es, ax
    xor bx, bx
    mov ax, 0x0201
    int 0x13
    jnc .ok
.failed:
    dec byte [retries]
    jz disk_error
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jmp .retry
.ok:
    xor ax, ax
    mov es, ax
    ret

disk_error:
    mov si, msg_disk_error
    call print
    mov dx, ax
    call print_hex
halt:
    cli
    hlt
    jmp halt

%include "print16.inc"

; ------------------------------------------------------------------ data
boot_drive:     db 0
use_lba:        db 0
retries:        db 0
spt:            dw 0
heads:          dw 0
buf_seg:        dw 0
remaining:      dw 0
lba:            dd 0

align 4
dap:                                    ; disk address packet for int 0x13, ah = 0x42
    db 0x10, 0                          ; packet size, reserved
    dw 1                                ; sectors to transfer
    dw 0                                ; buffer offset
.segment:   dw 0                        ; buffer segment
.lba:       dq 0                        ; starting LBA

msg_stage2:     db "DrakeOS stage2", 13, 10, 0
msg_a20:        db "Cannot enable A20", 13, 10, 0
msg_loading:    db "Loading kernel", 0
msg_dot:        db ".", 0
msg_ok:         db " ok", 13, 10, 0
msg_pm:         db "Entering protected mode", 13, 10, 0
msg_disk_error: db 13, 10, "Disk read error ", 0
loader_name:    db "DrakeOS stage2", 0

; Flat 4 GB code and data segments (base 0, limit 0xFFFFF pages).
align 8
gdt_start:
    dq 0                                ; null descriptor
gdt_code:
    dw 0xffff, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00 ; present, ring 0, code, readable; 4 KB granularity, 32-bit
gdt_data:
    dw 0xffff, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00 ; present, ring 0, data, writable
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ------------------------------------------------------------------ 32-bit part
[bits 32]
protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7c00

    ; Copy the kernel image from the staging buffer to its load address (1 MB).
    cld
    mov esi, BUFFER_SEG << 4
    mov edi, KERNEL_PHYS
    mov ecx, KERNEL_SECTORS * 512 / 4
    rep movsd

    mov eax, MB_MAGIC
    mov ebx, MBI_ADDR
    jmp CODE_SEG:KERNEL_PHYS
