[BITS 64]

global idt64_load
global isr_default_asm
global isr_page_fault_asm
global isr_gp_fault_asm
global isr_double_fault_asm
global irq_keyboard_asm
global irq_timer_asm

extern default_interrupt_handler64
extern page_fault_handler64
extern gp_fault_handler64
extern double_fault_handler64
extern keyboard_handler64
extern timer_handler64

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
    sub rsp, 8
    call page_fault_handler64
.hang_pf:
    cli
    hlt
    jmp .hang_pf

isr_gp_fault_asm:
    cli
    PUSH_GPRS
    mov rdi, [rsp + 120]
    sub rsp, 8
    call gp_fault_handler64
.hang_gp:
    cli
    hlt
    jmp .hang_gp

isr_double_fault_asm:
    cli
    PUSH_GPRS
    mov rdi, [rsp + 120]
    sub rsp, 8
    call double_fault_handler64
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
    add rsp, 8
    POP_GPRS
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
