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
    lea rdi, [rel cmd_version]
    call match_exact
    test al, al
    jnz .do_version

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_bootinfo]
    call match_exact
    test al, al
    jnz .do_bootinfo

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_memstat]
    call match_exact
    test al, al
    jnz .do_memstat

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_uptime]
    call match_exact
    test al, al
    jnz .do_uptime

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_pid]
    call match_exact
    test al, al
    jnz .do_pid

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_ppid]
    call match_exact
    test al, al
    jnz .do_ppid

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_ps]
    call match_exact
    test al, al
    jnz .do_ps

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_ls]
    call match_exact
    test al, al
    jnz .do_ls

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_echo]
    call match_prefix
    test al, al
    jnz .do_echo

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_cat]
    call match_prefix
    test al, al
    jnz .do_cat

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_run]
    call match_prefix
    test al, al
    jnz .do_run

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_rm]
    call match_prefix
    test al, al
    jnz .do_rm

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_touch]
    call match_prefix
    test al, al
    jnz .do_touch

    lea rsi, [rel input_buffer]
    lea rdi, [rel cmd_save]
    call match_prefix
    test al, al
    jnz .do_save

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

.do_version:
    sys_version
    jmp .shell_loop

.do_bootinfo:
    sys_bootinfo
    jmp .shell_loop

.do_memstat:
    sys_memstat
    jmp .shell_loop

.do_uptime:
    sys_uptime
    jmp .shell_loop

.do_pid:
    sys_write pid_msg, pid_msg_end - pid_msg
    sys_get_pid
    mov ebx, eax
    call print_hex32_eax
    sys_write newline_msg, newline_msg_end - newline_msg
    jmp .shell_loop

.do_ppid:
    sys_write ppid_msg, ppid_msg_end - ppid_msg
    sys_get_ppid
    mov ebx, eax
    call print_hex32_eax
    sys_write newline_msg, newline_msg_end - newline_msg
    jmp .shell_loop

.do_ps:
    sys_ps
    jmp .shell_loop

.do_ls:
    sys_list_files
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

.do_cat:
    lea rdi, [rel input_buffer]
    add rdi, 4
    cmp byte [rdi], 0
    je .cat_usage
    sys_cat_reg rdi
    jmp .shell_loop

.cat_usage:
    sys_write cat_usage_msg, cat_usage_msg_end - cat_usage_msg
    jmp .shell_loop

.do_run:
    lea rdi, [rel input_buffer]
    add rdi, 4
    cmp byte [rdi], 0
    je .run_usage
    sys_run_reg rdi
    jmp .shell_loop

.run_usage:
    sys_write run_usage_msg, run_usage_msg_end - run_usage_msg
    jmp .shell_loop

.do_rm:
    lea rdi, [rel input_buffer]
    add rdi, 3
    cmp byte [rdi], 0
    je .rm_usage
    sys_rm_reg rdi
    jmp .shell_loop

.rm_usage:
    sys_write rm_usage_msg, rm_usage_msg_end - rm_usage_msg
    jmp .shell_loop

.do_touch:
    lea rdi, [rel input_buffer]
    add rdi, 6
    cmp byte [rdi], 0
    je .touch_usage
    sys_touch_reg rdi
    jmp .shell_loop

.touch_usage:
    sys_write touch_usage_msg, touch_usage_msg_end - touch_usage_msg
    jmp .shell_loop

.do_save:
    lea rdi, [rel input_buffer]
    add rdi, 5
    cmp byte [rdi], 0
    je .save_usage
    mov rsi, rdi

.save_find_text:
    mov al, [rsi]
    cmp al, 0
    je .save_usage
    cmp al, ' '
    je .save_split
    inc rsi
    jmp .save_find_text

.save_split:
    mov byte [rsi], 0
    inc rsi
    cmp byte [rsi], 0
    je .save_usage
    sys_save_regs rdi, rsi
    jmp .shell_loop

.save_usage:
    sys_write save_usage_msg, save_usage_msg_end - save_usage_msg
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

print_hex32_eax:
    mov eax, ebx
    mov byte [rel hex_buffer], '0'
    mov byte [rel hex_buffer + 1], 'x'
    lea rdi, [rel hex_buffer + 9]
    mov ecx, 8
.hex_loop:
    mov edx, eax
    and edx, 0x0F
    cmp dl, 9
    jbe .hex_digit
    add dl, 'A' - 10
    jmp .hex_store
.hex_digit:
    add dl, '0'
.hex_store:
    mov [rdi], dl
    shr eax, 4
    dec rdi
    loop .hex_loop
    sys_write hex_buffer, 10
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
    db 'Commands: help, echo [text], about, version, bootinfo, memstat, uptime', 10
    db '          pid, ppid, ps, clear, ls, cat [file], run [file], rm [file], touch [file]', 10
    db '          save [file] [text], exit', 10
help_msg_end:
about_msg:
    db 'USHELL.BIN runs entirely in user mode using int 0x80 syscalls.', 10
about_msg_end:
pid_msg:
    db 'pid: '
pid_msg_end:
ppid_msg:
    db 'ppid: '
ppid_msg_end:
cat_usage_msg:
    db 'Usage: cat [filename]', 10
cat_usage_msg_end:
run_usage_msg:
    db 'Usage: run [filename]', 10
run_usage_msg_end:
rm_usage_msg:
    db 'Usage: rm [filename]', 10
rm_usage_msg_end:
touch_usage_msg:
    db 'Usage: touch [filename]', 10
touch_usage_msg_end:
save_usage_msg:
    db 'Usage: save [filename] [text]', 10
save_usage_msg_end:
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
cmd_version:
    db 'version', 0
cmd_bootinfo:
    db 'bootinfo', 0
cmd_memstat:
    db 'memstat', 0
cmd_uptime:
    db 'uptime', 0
cmd_pid:
    db 'pid', 0
cmd_ppid:
    db 'ppid', 0
cmd_ps:
    db 'ps', 0
cmd_ls:
    db 'ls', 0
cmd_cat:
    db 'cat ', 0
cmd_run:
    db 'run ', 0
cmd_rm:
    db 'rm ', 0
cmd_touch:
    db 'touch ', 0
cmd_save:
    db 'save ', 0
cmd_clear:
    db 'clear', 0
cmd_exit:
    db 'exit', 0

section .bss

input_buffer:
    resb INPUT_BUFFER_SIZE
input_len:
    resb 1
hex_buffer:
    resb 10
