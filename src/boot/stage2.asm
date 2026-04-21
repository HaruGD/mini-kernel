[BITS 16]
[ORG 0x0000]

CODE_OFFSET equ 0x08
DATA_OFFSET equ 0x10

STAGE2_PHYS_ADDR equ 0x90000
KERNEL_START_ADDR equ 0x1000
BOOT_INFO_ADDR equ 0x8000
BOOT_INFO_MAGIC equ 0x4649424D
BOOT_INFO_VERSION equ 1
BOOT_INFO_SIZE equ 32
REAL_MODE_STACK_TOP equ 0x7c00
PROTECTED_MODE_STACK_TOP equ 0x9FC00
SECTOR_SIZE equ 512

DISK_READ_FN equ 0x02
BIOS_TTY_FN equ 0x0e
VIDEO_PAGE equ 0x00
TEXT_ATTR equ 0x07

SECTORS_PER_TRACK equ 63
HEAD_COUNT equ 16
SECTORS_PER_CYLINDER equ SECTORS_PER_TRACK * HEAD_COUNT

%ifndef STAGE2_SECTOR_COUNT
%define STAGE2_SECTOR_COUNT 4
%endif

%ifndef KERNEL_SECTOR_COUNT
%define KERNEL_SECTOR_COUNT 96
%endif

KERNEL_START_LBA equ 1 + STAGE2_SECTOR_COUNT

stage2_start:
    cli
    mov ax, cs
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, REAL_MODE_STACK_TOP
    sti

    mov [boot_drive], dl
    mov word [current_lba], KERNEL_START_LBA
    mov word [dest_offset], KERNEL_START_ADDR
    mov si, KERNEL_SECTOR_COUNT

load_kernel_loop:
    cmp si, 0
    je kernel_loaded

    mov ax, [current_lba]
    xor dx, dx
    mov bx, SECTORS_PER_CYLINDER
    div bx
    mov ch, al

    mov ax, dx
    xor dx, dx
    mov bx, SECTORS_PER_TRACK
    div bx
    mov dh, al
    mov cl, dl
    inc cl

    xor ax, ax
    mov es, ax
    mov bx, [dest_offset]
    mov ah, DISK_READ_FN
    mov al, 0x01
    mov dl, [boot_drive]
    int 0x13
    jc disk_read_error

    add word [dest_offset], SECTOR_SIZE
    inc word [current_lba]
    dec si
    jmp load_kernel_loop

kernel_loaded:
    call write_boot_info

load_pm:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_OFFSET:(STAGE2_PHYS_ADDR + protected_mode)

disk_read_error:
    mov [disk_error_code], ah
    cli
    mov si, disk_error_msg
    call print_string
    mov al, [disk_error_code]
    call print_hex8
    mov si, newline_msg
    call print_string
.hang:
    hlt
    jmp .hang

write_boot_info:
    push ax
    push di

    xor ax, ax
    mov es, ax
    mov di, BOOT_INFO_ADDR

    mov dword [es:di + 0], BOOT_INFO_MAGIC
    mov dword [es:di + 4], BOOT_INFO_VERSION
    mov dword [es:di + 8], BOOT_INFO_SIZE
    xor eax, eax
    mov al, [boot_drive]
    mov dword [es:di + 12], eax
    mov dword [es:di + 16], KERNEL_START_ADDR
    mov dword [es:di + 20], KERNEL_SECTOR_COUNT
    mov dword [es:di + 24], STAGE2_PHYS_ADDR
    mov dword [es:di + 28], 0

    pop di
    pop ax
    ret

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, BIOS_TTY_FN
    mov bh, VIDEO_PAGE
    mov bl, TEXT_ATTR
    int 0x10
    jmp print_string
.done:
    ret

print_hex8:
    push ax
    shr al, 4
    call print_hex_nibble
    pop ax
    and al, 0x0f
    call print_hex_nibble
    ret

print_hex_nibble:
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp .emit
.digit:
    add al, '0'
.emit:
    mov ah, BIOS_TTY_FN
    mov bh, VIDEO_PAGE
    mov bl, TEXT_ATTR
    int 0x10
    ret

gdt_start:
    dq 0

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd STAGE2_PHYS_ADDR + gdt_start

boot_drive:
    db 0

disk_error_code:
    db 0

current_lba:
    dw 0

dest_offset:
    dw 0

disk_error_msg:
    db 'Stage2 disk read failed: 0x', 0

newline_msg:
    db 13, 10, 0

[BITS 32]
protected_mode:
    mov ax, DATA_OFFSET
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ebp, PROTECTED_MODE_STACK_TOP
    mov esp, ebp

    in al, 0x92
    or al, 2
    out 0x92, al

    mov eax, BOOT_INFO_ADDR
    jmp CODE_OFFSET:KERNEL_START_ADDR
