[BITS 64]

%include "src/user/include/syscall.inc"

section .text

global _start

_start:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    sys_write user_msg, user_msg_end - user_msg
    sys_write user_prompt, user_prompt_end - user_prompt
    sys_write user_hint, user_hint_end - user_hint

.wait_key:
    sys_getchar
    mov [rel user_last_char], al
    cmp byte [rel user_last_char], 'q'
    je .exit_user
    sys_write user_pressed_msg, user_pressed_msg_end - user_pressed_msg
    sys_putchar_mem8 [rel user_last_char]
    sys_write user_newline, user_newline_end - user_newline
    jmp .wait_key

.exit_user:
    sys_write user_exit_msg, user_exit_msg_end - user_exit_msg
    movzx edi, byte [rel user_last_char]
    sys_exit_reg rdi

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
