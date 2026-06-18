[BITS 64]

global gdt64_load
global tss64_load

gdt64_load:
    lgdt [rdi]
    push qword 0x18
    lea rax, [rel gdt64_reload_cs]
    push rax
    o64 retf
gdt64_reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

tss64_load:
    ltr di
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
