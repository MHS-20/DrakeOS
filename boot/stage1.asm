; DrakeOS stage 1: the 512-byte boot sector.
;
; BIOS loads this sector at 0x7C00 and jumps to it with DL = boot drive.
; It loads STAGE2_SECTORS sectors that follow it (LBA 1..) to 0x8000 and jumps there.

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif

STAGE2_ADDR equ 0x8000

[org 0x7c00]
[bits 16]
    jmp 0:start                 ; normalise CS:IP; some BIOSes enter at 07C0:0000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00              ; stack grows down from just below us
    sti
    cld
    mov [boot_drive], dl

    mov si, msg_boot
    call print

    ; int 0x13, ah = 0x02: read AL sectors from cylinder CH, head DH, sector CL into ES:BX.
    ; Stage 2 sits on the first track (sectors 2..9), which every disk geometry provides.
    mov di, 3                   ; retries: floppies may need the motor to spin up
.read:
    mov ah, 0x02
    mov al, STAGE2_SECTORS
    xor ch, ch
    mov cl, 2
    xor dh, dh
    mov dl, [boot_drive]
    mov bx, STAGE2_ADDR
    int 0x13
    jnc .loaded
    xor ah, ah                  ; reset the drive and try again
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .read
    jmp disk_error
.loaded:
    cmp al, STAGE2_SECTORS
    jne disk_error

    mov dl, [boot_drive]
    jmp 0:STAGE2_ADDR

disk_error:
    mov si, msg_disk_error
    call print
    mov dx, ax                  ; ah = BIOS status code
    call print_hex
.halt:
    cli
    hlt
    jmp .halt

%include "print16.inc"

boot_drive:     db 0
msg_boot:       db "DrakeOS stage1", 13, 10, 0
msg_disk_error: db "Disk read error ", 0

times 510 - ($ - $$) db 0
dw 0xaa55
