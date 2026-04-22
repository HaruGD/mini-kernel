[BITS 16]
[ORG 0x7c00]

STAGE2_LOAD_SEG equ 0x9000
STAGE2_LOAD_OFF equ 0x0000

%ifndef STAGE2_SECTOR_COUNT
%define STAGE2_SECTOR_COUNT 8
%endif

jmp short start
nop

db 'MINIKERN'
dw 512
db 1
dw 1 + STAGE2_SECTOR_COUNT
db 2
dw 224
dw 2880
db 0xF0
dw 9
dw 63
dw 16
dd 0
dd 0
db 0
db 0
db 0x29
dd 0x20260422
db 'MINIKERNEL '
db 'FAT12   '

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl

    mov ax, STAGE2_LOAD_SEG
    mov es, ax
    mov bx, STAGE2_LOAD_OFF
    mov ah, 0x02
    mov al, STAGE2_SECTOR_COUNT
    mov ch, 0x00
    mov cl, 0x02
    mov dh, 0x00
    mov dl, [boot_drive]
    int 0x13
    jc disk_read_error

    mov dl, [boot_drive]
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

disk_read_error:
    cli
.hang:
    hlt
    jmp .hang

boot_drive:
    db 0

times 510 - ($ - $$) db 0
dw 0xAA55
