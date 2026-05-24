[BITS 64]

global enter_user_mode
global resume_user_mode
global kernel_user_return_rsp
global kernel_user_saved_rbx
global kernel_user_saved_rbp
global kernel_user_saved_r12
global kernel_user_saved_r13
global kernel_user_saved_r14
global kernel_user_saved_r15
global kernel_user_resume_rax
global kernel_user_resume_rbx
global kernel_user_resume_rcx
global kernel_user_resume_rdx
global kernel_user_resume_rbp
global kernel_user_resume_rsi
global kernel_user_resume_rdi
global kernel_user_resume_r8
global kernel_user_resume_r9
global kernel_user_resume_r10
global kernel_user_resume_r11
global kernel_user_resume_r12
global kernel_user_resume_r13
global kernel_user_resume_r14
global kernel_user_resume_r15
global kernel_user_resume_rip
global kernel_user_resume_rsp
global kernel_user_resume_rflags

section .bss
align 8
kernel_user_return_rsp: resq 1
kernel_user_saved_rbx:  resq 1
kernel_user_saved_rbp:  resq 1
kernel_user_saved_r12:  resq 1
kernel_user_saved_r13:  resq 1
kernel_user_saved_r14:  resq 1
kernel_user_saved_r15:  resq 1
kernel_user_resume_rax: resq 1
kernel_user_resume_rbx: resq 1
kernel_user_resume_rcx: resq 1
kernel_user_resume_rdx: resq 1
kernel_user_resume_rbp: resq 1
kernel_user_resume_rsi: resq 1
kernel_user_resume_rdi: resq 1
kernel_user_resume_r8:  resq 1
kernel_user_resume_r9:  resq 1
kernel_user_resume_r10: resq 1
kernel_user_resume_r11: resq 1
kernel_user_resume_r12: resq 1
kernel_user_resume_r13: resq 1
kernel_user_resume_r14: resq 1
kernel_user_resume_r15: resq 1
kernel_user_resume_rip: resq 1
kernel_user_resume_rsp: resq 1
kernel_user_resume_rflags: resq 1

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

resume_user_mode:
    mov [kernel_user_return_rsp], rsp
    mov [kernel_user_saved_rbx], rbx
    mov [kernel_user_saved_rbp], rbp
    mov [kernel_user_saved_r12], r12
    mov [kernel_user_saved_r13], r13
    mov [kernel_user_saved_r14], r14
    mov [kernel_user_saved_r15], r15

    push qword 0x23
    mov rax, [kernel_user_resume_rsp]
    push rax
    mov rax, [kernel_user_resume_rflags]
    push rax
    push qword 0x2B
    mov rax, [kernel_user_resume_rip]
    push rax

    mov rax, [kernel_user_resume_rax]
    mov rbx, [kernel_user_resume_rbx]
    mov rcx, [kernel_user_resume_rcx]
    mov rdx, [kernel_user_resume_rdx]
    mov rbp, [kernel_user_resume_rbp]
    mov rsi, [kernel_user_resume_rsi]
    mov rdi, [kernel_user_resume_rdi]
    mov r8,  [kernel_user_resume_r8]
    mov r9,  [kernel_user_resume_r9]
    mov r10, [kernel_user_resume_r10]
    mov r11, [kernel_user_resume_r11]
    mov r12, [kernel_user_resume_r12]
    mov r13, [kernel_user_resume_r13]
    mov r14, [kernel_user_resume_r14]
    mov r15, [kernel_user_resume_r15]
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
