[BITS 64]

%include "src/user/include/syscall.inc"

%define INPUT_BUFFER_SIZE 64

section .text

global _start

_start:
    mov ax, 0x23
    mov ds, ax
    mov es, ax

    sys_write banner_msg, banner_msg_end - banner_msg

.shell_loop:
    sys_write prompt_msg, prompt_msg_end - prompt_msg
    mov byte [rel input_len], 0

.read_loop:
    sys_getchar

    cmp al, 10
    je .line_done
    cmp al, 13
    je .line_done

    cmp al, 8
    je .handle_backspace

    cmp al, 32
    jb .read_loop
    cmp al, 126
    ja .read_loop

    movzx rcx, byte [rel input_len]
    cmp rcx, INPUT_BUFFER_SIZE - 1
    jae .read_loop

    lea rdx, [rel input_buffer]
    mov [rdx + rcx], al
    inc byte [rel input_len]
    mov byte [rdx + rcx + 1], 0
    sys_putchar_reg al
    jmp .read_loop

.handle_backspace:
    cmp byte [rel input_len], 0
    je .read_loop
    dec byte [rel input_len]
    movzx rcx, byte [rel input_len]
    lea rdx, [rel input_buffer]
    mov byte [rdx + rcx], 0
    sys_putchar_imm 8
    sys_putchar_imm ' '
    sys_putchar_imm 8
    jmp .read_loop

.line_done:
    sys_write newline_msg, newline_msg_end - newline_msg

    movzx rcx, byte [rel input_len]
    lea rdx, [rel input_buffer]
    mov byte [rdx + rcx], 0

    cmp byte [rel input_buffer], 0
    je .shell_loop

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_help]
    call match_exact
    test al, al
    jnz .do_help

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_exit]
    call match_exact
    test al, al
    jnz .do_exit

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_clear]
    call match_exact
    test al, al
    jnz .do_clear

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_about]
    call match_exact
    test al, al
    jnz .do_about

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_echo]
    call match_prefix
    test al, al
    jnz .do_echo

    sys_write unknown_msg, unknown_msg_end - unknown_msg
    lea rsi, [rel input_buffer]
    call print_cstring
    sys_write newline_msg, newline_msg_end - newline_msg
    jmp .shell_loop

.do_help:
    sys_write help_msg, help_msg_end - help_msg
    jmp .shell_loop

.do_about:
    sys_write about_msg, about_msg_end - about_msg
    jmp .shell_loop

.do_clear:
    mov ecx, 24
.clear_loop:
    sys_write newline_msg, newline_msg_end - newline_msg
    loop .clear_loop
    jmp .shell_loop

.do_echo:
    lea rsi, [rel input_buffer + 5]
    call print_cstring
    sys_write newline_msg, newline_msg_end - newline_msg
    jmp .shell_loop

.do_exit:
    sys_write exit_msg, exit_msg_end - exit_msg
    sys_exit

match_exact:
.exact_loop:
    mov al, [rsi]
    mov dl, [rdi]
    cmp al, dl
    jne .exact_no
    test al, al
    je .exact_yes
    inc rsi
    inc rdi
    jmp .exact_loop
.exact_yes:
    mov al, 1
    ret
.exact_no:
    xor al, al
    ret

match_prefix:
.prefix_loop:
    mov al, [rdi]
    test al, al
    je .prefix_yes
    cmp [rsi], al
    jne .prefix_no
    inc rsi
    inc rdi
    jmp .prefix_loop
.prefix_yes:
    mov al, 1
    ret
.prefix_no:
    xor al, al
    ret

print_cstring:
    mov al, [rsi]
    test al, al
    je .print_done
    sys_putchar_reg al
    inc rsi
    jmp print_cstring
.print_done:
    ret

section .data

banner_msg:
    db '=== USHELL.BIN ===', 10
    db 'User shell ready. Type help for commands.', 10
banner_msg_end:
prompt_msg:
    db 'ush> '
prompt_msg_end:
help_msg:
    db 'Commands: help, echo [text], about, clear, exit', 10
help_msg_end:
about_msg:
    db 'USHELL.BIN runs entirely in user mode using int 0x80 syscalls.', 10
about_msg_end:
unknown_msg:
    db 'Unknown command: '
unknown_msg_end:
exit_msg:
    db 'Leaving user shell...', 10
exit_msg_end:
newline_msg:
    db 10
newline_msg_end:
cmd_help:
    db 'help', 0
cmd_echo:
    db 'echo ', 0
cmd_about:
    db 'about', 0
cmd_clear:
    db 'clear', 0
cmd_exit:
    db 'exit', 0

section .bss

input_buffer:
    resb INPUT_BUFFER_SIZE
input_len:
    resb 1
