[BITS 64]

global enter_user_mode
global kernel_user_return_rsp
global kernel_user_saved_rbx
global kernel_user_saved_rbp
global kernel_user_saved_r12
global kernel_user_saved_r13
global kernel_user_saved_r14
global kernel_user_saved_r15

section .bss
align 8
kernel_user_return_rsp: resq 1
kernel_user_saved_rbx:  resq 1
kernel_user_saved_rbp:  resq 1
kernel_user_saved_r12:  resq 1
kernel_user_saved_r13:  resq 1
kernel_user_saved_r14:  resq 1
kernel_user_saved_r15:  resq 1

section .text

enter_user_mode:
    mov [kernel_user_return_rsp], rsp
    mov [kernel_user_saved_rbx], rbx
    mov [kernel_user_saved_rbp], rbp
    mov [kernel_user_saved_r12], r12
    mov [kernel_user_saved_r13], r13
    mov [kernel_user_saved_r14], r14
    mov [kernel_user_saved_r15], r15
    push qword 0x23
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push qword 0x2B
    push rdi
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
