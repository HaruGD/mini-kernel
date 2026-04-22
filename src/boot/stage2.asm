[BITS 16]
[ORG 0x0000]

CODE_OFFSET equ 0x08
DATA_OFFSET equ 0x10

STAGE2_PHYS_ADDR equ 0x90000
KERNEL_START_ADDR equ 0x1000
BOOT_INFO_ADDR equ 0x8000
BOOT_INFO_MAGIC equ 0x4649424D
BOOT_INFO_VERSION equ 1
BOOT_INFO_SIZE equ 44
E820_MEMORY_MAP_ADDR equ 0x8200
E820_ENTRY_SIZE equ 24
E820_MAX_ENTRIES equ 16
SCRATCH_BUFFER equ 0x8400
REAL_MODE_STACK_TOP equ 0x7c00
PROTECTED_MODE_STACK_TOP equ 0x9FC00
SECTOR_SIZE equ 512

DISK_READ_FN equ 0x02
E820_FN equ 0xE820
E820_MAGIC equ 0x534D4150
BIOS_TTY_FN equ 0x0e
VIDEO_PAGE equ 0x00
TEXT_ATTR equ 0x07

SECTORS_PER_TRACK equ 63
HEAD_COUNT equ 16
SECTORS_PER_CYLINDER equ SECTORS_PER_TRACK * HEAD_COUNT
FAT_COUNT equ 2
FAT_SECTORS equ 9
ROOT_DIR_ENTRIES equ 224
ROOT_DIR_SECTORS equ 14

%ifndef STAGE2_SECTOR_COUNT
%define STAGE2_SECTOR_COUNT 8
%endif

FAT_START_LBA equ 1 + STAGE2_SECTOR_COUNT
ROOT_DIR_START_LBA equ FAT_START_LBA + (FAT_COUNT * FAT_SECTORS)
DATA_START_LBA equ ROOT_DIR_START_LBA + ROOT_DIR_SECTORS

stage2_start:
    cli
    cld
    mov ax, cs
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, REAL_MODE_STACK_TOP
    sti

    mov [boot_drive], dl
    call find_kernel_file
    call load_kernel_file

kernel_loaded:
    call collect_memory_map
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

file_not_found_error:
    cli
    mov si, kernel_not_found_msg
    call print_string
    mov si, newline_msg
    call print_string
.hang:
    hlt
    jmp .hang

cluster_chain_error:
    cli
    mov si, cluster_chain_error_msg
    call print_string
    mov si, newline_msg
    call print_string
.hang:
    hlt
    jmp .hang

find_kernel_file:
    mov word [root_lba], ROOT_DIR_START_LBA
    mov word [root_sectors_left], ROOT_DIR_SECTORS

.read_root_sector:
    cmp word [root_sectors_left], 0
    je file_not_found_error

    xor ax, ax
    mov es, ax
    mov bx, SCRATCH_BUFFER
    mov ax, [root_lba]
    call read_sector_lba

    mov di, SCRATCH_BUFFER
    mov cx, 16

.check_entry:
    cmp byte [es:di], 0x00
    je file_not_found_error
    cmp byte [es:di], 0xE5
    je .next_entry
    cmp byte [es:di + 11], 0x0F
    je .next_entry

    push cx
    push di
    mov si, kernel_filename
    mov cx, 11
    repe cmpsb
    pop di
    pop cx
    je .found

.next_entry:
    add di, 32
    loop .check_entry

    inc word [root_lba]
    dec word [root_sectors_left]
    jmp .read_root_sector

.found:
    mov ax, [es:di + 26]
    mov [kernel_start_cluster], ax
    mov eax, [es:di + 28]
    mov [kernel_file_size], eax
    add eax, SECTOR_SIZE - 1
    shr eax, 9
    mov [kernel_sector_count], ax
    cmp ax, 0
    je file_not_found_error
    ret

load_kernel_file:
    mov ax, [kernel_start_cluster]
    mov [current_cluster], ax
    mov ax, [kernel_sector_count]
    mov [sectors_left], ax
    mov dword [dest_addr], KERNEL_START_ADDR

.read_cluster:
    cmp word [sectors_left], 0
    je .done

    mov ax, [current_cluster]
    sub ax, 2
    add ax, DATA_START_LBA
    call read_sector_to_kernel

    dec word [sectors_left]
    cmp word [sectors_left], 0
    je .done

    mov ax, [current_cluster]
    call get_next_cluster
    cmp ax, 0x0FF8
    jae cluster_chain_error
    cmp ax, 2
    jb cluster_chain_error
    mov [current_cluster], ax
    jmp .read_cluster

.done:
    ret

get_next_cluster:
    push bx
    push dx

    mov bx, ax
    shr ax, 1
    add ax, bx
    mov [fat_entry_byte_offset], ax

    xor dx, dx
    mov bx, SECTOR_SIZE
    div bx
    add ax, FAT_START_LBA
    mov [fat_entry_sector], ax
    mov [fat_entry_sector_offset], dx

    xor bx, bx
    mov es, bx
    mov bx, SCRATCH_BUFFER
    call read_sector_lba

    cmp word [fat_entry_sector_offset], SECTOR_SIZE - 1
    jne .entry_loaded

    mov ax, [fat_entry_sector]
    inc ax
    mov bx, SCRATCH_BUFFER + SECTOR_SIZE
    call read_sector_lba

.entry_loaded:
    mov bx, SCRATCH_BUFFER
    add bx, [fat_entry_sector_offset]
    mov ax, [es:bx]

    test word [current_cluster], 1
    jz .even_cluster
    shr ax, 4
    jmp .done

.even_cluster:
    and ax, 0x0FFF

.done:
    pop dx
    pop bx
    ret

read_sector_to_kernel:
    push ax
    mov eax, [dest_addr]
    shr eax, 4
    mov es, ax
    xor bx, bx
    pop ax
    call read_sector_lba
    add dword [dest_addr], SECTOR_SIZE
    ret

read_sector_lba:
    mov [read_lba], ax
    mov [read_buffer_offset], bx

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

    mov bx, [read_buffer_offset]
    mov ah, DISK_READ_FN
    mov al, 0x01
    mov dl, [boot_drive]
    int 0x13
    jc disk_read_error
    ret

collect_memory_map:
    push es

    xor ax, ax
    mov es, ax
    mov di, E820_MEMORY_MAP_ADDR
    xor ebx, ebx
    xor bp, bp

.next_entry:
    cmp bp, E820_MAX_ENTRIES
    jae .done

    mov dword [es:di + 20], 1
    mov eax, E820_FN
    mov edx, E820_MAGIC
    mov ecx, E820_ENTRY_SIZE
    int 0x15
    jc .done

    cmp eax, E820_MAGIC
    jne .unsupported
    cmp ecx, 20
    jb .done

    mov eax, [es:di + 8]
    or eax, [es:di + 12]
    jz .skip_entry

    inc bp
    add di, E820_ENTRY_SIZE

.skip_entry:
    test ebx, ebx
    jnz .next_entry
    jmp .done

.unsupported:
    xor bp, bp

.done:
    mov [memory_map_count], bp
    pop es
    ret

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
    xor eax, eax
    mov ax, [kernel_sector_count]
    mov dword [es:di + 20], eax
    mov dword [es:di + 24], STAGE2_PHYS_ADDR
    mov dword [es:di + 28], E820_MEMORY_MAP_ADDR
    xor eax, eax
    mov ax, [memory_map_count]
    mov dword [es:di + 32], eax
    mov dword [es:di + 36], E820_ENTRY_SIZE
    mov dword [es:di + 40], 0

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

read_lba:
    dw 0

read_buffer_offset:
    dw 0

root_lba:
    dw 0

root_sectors_left:
    dw 0

kernel_start_cluster:
    dw 0

current_cluster:
    dw 0

kernel_sector_count:
    dw 0

sectors_left:
    dw 0

kernel_file_size:
    dd 0

dest_addr:
    dd 0

fat_entry_byte_offset:
    dw 0

fat_entry_sector:
    dw 0

fat_entry_sector_offset:
    dw 0

memory_map_count:
    dw 0

kernel_filename:
    db 'KERNEL  BIN'

disk_error_msg:
    db 'Stage2 disk read failed: 0x', 0

kernel_not_found_msg:
    db 'KERNEL.BIN not found', 0

cluster_chain_error_msg:
    db 'FAT12 cluster chain error', 0

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
