[BITS 64]

global _start64

extern kernel64_main

_start64:
    mov rsp, 0x1FF000
    mov rbp, rsp
    call kernel64_main

.hang:
    cli
    hlt
    jmp .hang
