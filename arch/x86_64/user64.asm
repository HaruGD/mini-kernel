[BITS 64]

%include "include/arch/x86_64/cpu_local_offsets.inc"

global enter_user_mode
global resume_user_mode
extern syscall_cpu_states

%macro MARK_USER_ACTIVE 0
    mov eax, [gs:CPU_LOCAL_LOGICAL_ID]
    shl rax, 12
    add rax, syscall_cpu_states
    mov byte [rax + 53], 1
%endmacro

section .text

enter_user_mode:
    mov [gs:CPU_LOCAL_USER_RETURN_RSP], rsp
    mov [gs:CPU_LOCAL_USER_SAVED_RBX], rbx
    mov [gs:CPU_LOCAL_USER_SAVED_RBP], rbp
    mov [gs:CPU_LOCAL_USER_SAVED_R12], r12
    mov [gs:CPU_LOCAL_USER_SAVED_R13], r13
    mov [gs:CPU_LOCAL_USER_SAVED_R14], r14
    mov [gs:CPU_LOCAL_USER_SAVED_R15], r15
    push qword 0x23
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push qword 0x2B
    push rdi
    MARK_USER_ACTIVE
    iretq

resume_user_mode:
    mov [gs:CPU_LOCAL_USER_RETURN_RSP], rsp
    mov [gs:CPU_LOCAL_USER_SAVED_RBX], rbx
    mov [gs:CPU_LOCAL_USER_SAVED_RBP], rbp
    mov [gs:CPU_LOCAL_USER_SAVED_R12], r12
    mov [gs:CPU_LOCAL_USER_SAVED_R13], r13
    mov [gs:CPU_LOCAL_USER_SAVED_R14], r14
    mov [gs:CPU_LOCAL_USER_SAVED_R15], r15

    push qword 0x23
    mov rax, [gs:CPU_LOCAL_USER_RESUME_RSP]
    push rax
    mov rax, [gs:CPU_LOCAL_USER_RESUME_RFLAGS]
    push rax
    push qword 0x2B
    mov rax, [gs:CPU_LOCAL_USER_RESUME_RIP]
    push rax

    MARK_USER_ACTIVE


    mov rax, [gs:CPU_LOCAL_USER_RESUME_RAX]
    mov rbx, [gs:CPU_LOCAL_USER_RESUME_RBX]
    mov rcx, [gs:CPU_LOCAL_USER_RESUME_RCX]
    mov rdx, [gs:CPU_LOCAL_USER_RESUME_RDX]
    mov rbp, [gs:CPU_LOCAL_USER_RESUME_RBP]
    mov rsi, [gs:CPU_LOCAL_USER_RESUME_RSI]
    mov rdi, [gs:CPU_LOCAL_USER_RESUME_RDI]
    mov r8,  [gs:CPU_LOCAL_USER_RESUME_R8]
    mov r9,  [gs:CPU_LOCAL_USER_RESUME_R9]
    mov r10, [gs:CPU_LOCAL_USER_RESUME_R10]
    mov r11, [gs:CPU_LOCAL_USER_RESUME_R11]
    mov r12, [gs:CPU_LOCAL_USER_RESUME_R12]
    mov r13, [gs:CPU_LOCAL_USER_RESUME_R13]
    mov r14, [gs:CPU_LOCAL_USER_RESUME_R14]
    mov r15, [gs:CPU_LOCAL_USER_RESUME_R15]
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
