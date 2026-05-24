[BITS 64]

section .text

global _start

_start:
    mov ax, 0x23
    mov ds, ax
    mov es, ax

    mov rax, 1
    lea rdi, [rel title_msg]
    mov rsi, title_msg_end - title_msg
    int 0x80

    mov rax, 1
    lea rdi, [rel body_msg]
    mov rsi, body_msg_end - body_msg
    int 0x80

    mov rax, 1
    lea rdi, [rel prompt_msg]
    mov rsi, prompt_msg_end - prompt_msg
    int 0x80

    mov rax, 4
    int 0x80
    mov [rel saved_char], al

    mov rax, 1
    lea rdi, [rel pressed_msg]
    mov rsi, pressed_msg_end - pressed_msg
    int 0x80

    movzx rdi, byte [rel saved_char]
    mov rax, 3
    int 0x80

    mov rax, 1
    lea rdi, [rel newline_msg]
    mov rsi, newline_msg_end - newline_msg
    int 0x80

    mov rax, 2
    int 0x80

.halt:
    jmp .halt

title_msg:
    db '=== UINFO.BIN ===', 10
title_msg_end:
body_msg:
    db 'This is the second user program loaded from the FAT12 image.', 10
    db 'The kernel loaded this file separately from UTEST.BIN.', 10
body_msg_end:
prompt_msg:
    db 'Press one key to return: '
prompt_msg_end:
pressed_msg:
    db 'You chose: '
pressed_msg_end:
newline_msg:
    db 10
newline_msg_end:
saved_char:
    db 0
