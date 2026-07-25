[BITS 16]
[ORG 0x7000]

%define MAILBOX_MAGIC       0x7800
%define MAILBOX_CR3         0x7808
%define MAILBOX_STACK_TOP   0x7810
%define MAILBOX_ENTRY       0x7818
%define MAILBOX_LOGICAL_ID  0x7820

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6FF0

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:protected_entry

[BITS 32]
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov eax, [MAILBOX_CR3]
    mov cr3, eax

    mov eax, cr4
    ; Match the BSP's architectural execution environment before entering
    ; compiler-generated C/C++ code. GCC may use SSE for ordinary structure
    ; copies, so every AP must enable OSFXSR/OSXMMEXCPT as well as PAE.
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11)
    wrmsr

    mov eax, cr0
    and eax, ~((1 << 2) | (1 << 3))
    or eax, 1 << 1
    or eax, 1 << 31
    mov cr0, eax
    jmp 0x18:long_mode_entry

[BITS 64]
long_mode_entry:
    fninit
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [MAILBOX_STACK_TOP]
    xor rbp, rbp
    mov edi, [MAILBOX_LOGICAL_ID]
    mov rax, [MAILBOX_ENTRY]
    jmp rax

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00AF9A000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt - 1
    dd gdt

times 256-($-$$) db 0
