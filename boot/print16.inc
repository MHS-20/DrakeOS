; Real-mode BIOS text output (int 0x10, ah = 0x0e teletype).
[bits 16]

; print: si = address of a zero-terminated string
print:
    pusha
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp .next
.done:
    popa
    ret

; print_hex: prints dx as 0xNNNN
print_hex:
    pusha
    mov si, .prefix
    call print
    mov cx, 4
.digit:
    rol dx, 4
    mov al, dl
    and al, 0x0f
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 'A' - '9' - 1
.out:
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    loop .digit
    popa
    ret
.prefix: db "0x", 0
