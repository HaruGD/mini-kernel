[BITS 64]

global uefi_enter_kernel

; SysV ABI from the UEFI C loader:
;   rdi = kernel entry physical address
;   rsi = BootInfo physical address
;   rdx = PML4 physical address
;   rcx = kernel stack top
uefi_enter_kernel:
    cli
    lgdt [rel uefi_gdtr]
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    o64 retf
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rax, rdx
    mov cr3, rax
    mov rsp, rcx
    mov rbp, rsp
    mov rax, rdi
    mov rdi, rsi
    jmp rax

align 8
uefi_gdt:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
uefi_gdt_end:

uefi_gdtr:
    dw uefi_gdt_end - uefi_gdt - 1
    dq uefi_gdt

section .note.GNU-stack noalloc noexec nowrite progbits
