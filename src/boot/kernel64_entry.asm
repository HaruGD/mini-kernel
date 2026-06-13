[BITS 64]

global _start64

extern kernel64_main
extern __init_array_start
extern __init_array_end

_start64:
    push rdi
    mov rbx, __init_array_start
.ctor_loop:
    cmp rbx, __init_array_end
    jae .ctor_done
    call [rbx]
    add rbx, 8
    jmp .ctor_loop
.ctor_done:
    pop rdi
    mov rbp, rsp
    call kernel64_main

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
