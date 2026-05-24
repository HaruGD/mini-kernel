[BITS 64]

section .text

global _start

_start:
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
