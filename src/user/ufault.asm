[BITS 64]

%include "src/user/include/syscall.inc"

section .text
global _start

_start:
    sys_write intro_msg, intro_msg_end - intro_msg
    mov rax, 0x0000000080000000
    mov qword [rax], 0x12345678
    sys_write after_msg, after_msg_end - after_msg
    sys_exit

section .rodata
intro_msg: db "=== UFAULT.BIN ===", 10, "Triggering a user-mode page fault...", 10, 0
intro_msg_end:
after_msg: db "This line should never print.", 10, 0
after_msg_end:
