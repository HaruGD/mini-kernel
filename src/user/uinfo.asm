[BITS 64]

%include "src/user/include/syscall.inc"

section .text

global _start

_start:
    mov ax, 0x23
    mov ds, ax
    mov es, ax

    sys_write title_msg, title_msg_end - title_msg
    sys_write body_msg, body_msg_end - body_msg
    sys_write prompt_msg, prompt_msg_end - prompt_msg
    sys_getchar
    mov [rel saved_char], al

    sys_write pressed_msg, pressed_msg_end - pressed_msg
    sys_putchar_mem8 [rel saved_char]
    sys_write newline_msg, newline_msg_end - newline_msg
    sys_exit

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
