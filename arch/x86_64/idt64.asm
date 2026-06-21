[BITS 64]

; Internal control tokens must never overlap sign-extended user error codes.
%define SYSCALL_RETURN_TO_KERNEL 0xFFFFFFFF80005301
%define SYSCALL_YIELD_TO_KERNEL  0xFFFFFFFF80005302
%define TIMER_PREEMPT_TO_KERNEL  0xFFFFFFFF80005303
%define SYSCALL_SLEEP_TO_KERNEL  0xFFFFFFFF80005304
%define FAULT_RETURN_TO_KERNEL 1

global idt64_load
global isr_default_asm
global isr_page_fault_asm
global isr_gp_fault_asm
global isr_double_fault_asm
global irq_keyboard_asm
global irq_timer_asm
global irq_spurious_asm
global user_test_asm
global user_exit_asm
global syscall_asm

extern default_interrupt_handler64
extern page_fault_handler64
extern gp_fault_handler64
extern double_fault_handler64
extern keyboard_handler64
extern timer_handler64
extern spurious_interrupt_handler64
extern user_test_interrupt_handler64
extern user_exit_interrupt_handler64
extern kernel_user_return_rsp
extern kernel_user_saved_rbx
extern kernel_user_saved_rbp
extern kernel_user_saved_r12
extern kernel_user_saved_r13
extern kernel_user_saved_r14
extern kernel_user_saved_r15
extern syscall_dispatch64
extern save_yield_context64
extern save_preempt_context64
extern save_sleep_context64

%macro PUSH_GPRS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_GPRS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

idt64_load:
    lidt [rdi]
    ret

isr_default_asm:
    cli
    PUSH_GPRS
    sub rsp, 8
    lea rdi, [rsp + 8]
    call default_interrupt_handler64
.hang_default:
    cli
    hlt
    jmp .hang_default

isr_page_fault_asm:
    cli
    mov rax, cr2
    PUSH_GPRS
    mov rdi, rax
    mov rsi, [rsp + 120]
    lea rdx, [rsp]
    sub rsp, 8
    call page_fault_handler64
    cmp rax, FAULT_RETURN_TO_KERNEL
    je fault_exit_asm
.hang_pf:
    cli
    hlt
    jmp .hang_pf

isr_gp_fault_asm:
    cli
    PUSH_GPRS
    mov rdi, [rsp + 120]
    lea rsi, [rsp]
    sub rsp, 8
    call gp_fault_handler64
    cmp rax, FAULT_RETURN_TO_KERNEL
    je fault_exit_asm
.hang_gp:
    cli
    hlt
    jmp .hang_gp

isr_double_fault_asm:
    cli
    PUSH_GPRS
    mov rdi, [rsp + 120]
    lea rsi, [rsp]
    sub rsp, 8
    call double_fault_handler64
    cmp rax, FAULT_RETURN_TO_KERNEL
    je fault_exit_asm
.hang_df:
    cli
    hlt
    jmp .hang_df

irq_keyboard_asm:
    PUSH_GPRS
    sub rsp, 8
    call keyboard_handler64
    add rsp, 8
    POP_GPRS
    iretq

irq_timer_asm:
    PUSH_GPRS
    sub rsp, 8
    call timer_handler64
    cmp rax, TIMER_PREEMPT_TO_KERNEL
    jne .timer_iret
    mov rax, [rsp + 136]
    test al, 0x3
    jz .timer_iret
    lea rdi, [rsp + 8]
    call save_preempt_context64
    add rsp, 8
    mov rbx, [kernel_user_saved_rbx]
    mov rbp, [kernel_user_saved_rbp]
    mov r12, [kernel_user_saved_r12]
    mov r13, [kernel_user_saved_r13]
    mov r14, [kernel_user_saved_r14]
    mov r15, [kernel_user_saved_r15]
    mov rsp, [kernel_user_return_rsp]
    ret
.timer_iret:
    add rsp, 8
    POP_GPRS
    iretq

irq_spurious_asm:
    PUSH_GPRS
    sub rsp, 8
    call spurious_interrupt_handler64
    add rsp, 8
    POP_GPRS
    iretq

user_test_asm:
    PUSH_GPRS
    sub rsp, 8
    call user_test_interrupt_handler64
    add rsp, 8
    POP_GPRS
    iretq

user_exit_asm:
    sub rsp, 8
    call user_exit_interrupt_handler64
    add rsp, 8
    mov rbx, [kernel_user_saved_rbx]
    mov rbp, [kernel_user_saved_rbp]
    mov r12, [kernel_user_saved_r12]
    mov r13, [kernel_user_saved_r13]
    mov r14, [kernel_user_saved_r14]
    mov r15, [kernel_user_saved_r15]
    mov rsp, [kernel_user_return_rsp]
    ret

syscall_asm:
    PUSH_GPRS
    sub rsp, 8
    mov rdi, [rsp + 120]
    mov rsi, [rsp + 72]
    mov rdx, [rsp + 80]
    mov rcx, [rsp + 96]
    call syscall_dispatch64
    cmp rax, SYSCALL_RETURN_TO_KERNEL
    je .syscall_exit
    cmp rax, SYSCALL_YIELD_TO_KERNEL
    je .syscall_yield
    cmp rax, SYSCALL_SLEEP_TO_KERNEL
    je .syscall_sleep
    mov [rsp + 120], rax
    add rsp, 8
    POP_GPRS
    iretq

.syscall_exit:
    add rsp, 8
    mov rbx, [kernel_user_saved_rbx]
    mov rbp, [kernel_user_saved_rbp]
    mov r12, [kernel_user_saved_r12]
    mov r13, [kernel_user_saved_r13]
    mov r14, [kernel_user_saved_r14]
    mov r15, [kernel_user_saved_r15]
    mov rsp, [kernel_user_return_rsp]
    ret

.syscall_yield:
    lea rdi, [rsp + 8]
    call save_yield_context64
    add rsp, 8
    mov rbx, [kernel_user_saved_rbx]
    mov rbp, [kernel_user_saved_rbp]
    mov r12, [kernel_user_saved_r12]
    mov r13, [kernel_user_saved_r13]
    mov r14, [kernel_user_saved_r14]
    mov r15, [kernel_user_saved_r15]
    mov rsp, [kernel_user_return_rsp]
    ret

.syscall_sleep:
    mov rsi, [rsp + 72]
    lea rdi, [rsp + 8]
    call save_sleep_context64
    add rsp, 8
    mov rbx, [kernel_user_saved_rbx]
    mov rbp, [kernel_user_saved_rbp]
    mov r12, [kernel_user_saved_r12]
    mov r13, [kernel_user_saved_r13]
    mov r14, [kernel_user_saved_r14]
    mov r15, [kernel_user_saved_r15]
    mov rsp, [kernel_user_return_rsp]
    ret

fault_exit_asm:
    add rsp, 8
    mov rbx, [kernel_user_saved_rbx]
    mov rbp, [kernel_user_saved_rbp]
    mov r12, [kernel_user_saved_r12]
    mov r13, [kernel_user_saved_r13]
    mov r14, [kernel_user_saved_r14]
    mov r15, [kernel_user_saved_r15]
    mov rsp, [kernel_user_return_rsp]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
