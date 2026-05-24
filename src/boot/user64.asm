[BITS 64]

global user_test_code_start
global user_test_code_end
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

user_test_code_start:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov rax, 1
    lea rdi, [rel user_msg]
    mov rsi, user_msg_end - user_msg
    int 0x80
    mov rax, 1
    lea rdi, [rel user_prompt]
    mov rsi, user_prompt_end - user_prompt
    int 0x80
    mov rax, 1
    lea rdi, [rel user_hint]
    mov rsi, user_hint_end - user_hint
    int 0x80
.wait_key:
    mov rax, 4
    int 0x80
    mov [rel user_last_char], al
    cmp byte [rel user_last_char], 'q'
    je .exit_user
    mov rax, 1
    lea rdi, [rel user_pressed_msg]
    mov rsi, user_pressed_msg_end - user_pressed_msg
    int 0x80
    movzx rdi, byte [rel user_last_char]
    mov rax, 3
    int 0x80
    mov rax, 1
    lea rdi, [rel user_newline]
    mov rsi, user_newline_end - user_newline
    int 0x80
    jmp .wait_key
.exit_user:
    mov rax, 1
    lea rdi, [rel user_exit_msg]
    mov rsi, user_exit_msg_end - user_exit_msg
    int 0x80
    mov rax, 2
    int 0x80
.halt:
    jmp .halt
user_msg:
    db 'Hello from user mode via syscall', 10
user_msg_end:
user_prompt:
    db 'User-mode input loop ready.', 10
user_prompt_end:
user_hint:
    db 'Press keys in user mode. Press q to exit.', 10
user_hint_end:
user_pressed_msg:
    db 'You pressed: '
user_pressed_msg_end:
user_newline:
    db 10
user_newline_end:
user_exit_msg:
    db 'Leaving user mode...', 10
user_exit_msg_end:
user_last_char:
    db 0
user_test_code_end:

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
