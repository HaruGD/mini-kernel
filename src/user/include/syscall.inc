%ifndef USER_SYSCALL_INC
%define USER_SYSCALL_INC

%define SYS_WRITE   1
%define SYS_EXIT    2
%define SYS_PUTCHAR 3
%define SYS_GETCHAR 4
%define SYS_LIST_FILES 5
%define SYS_CAT_FILE   6
%define SYS_RUN_USER   7
%define SYS_VERSION    8
%define SYS_BOOTINFO   9
%define SYS_MEMSTAT    10
%define SYS_RM_FILE    11
%define SYS_UPTIME     12
%define SYS_TOUCH_FILE 13
%define SYS_SAVE_FILE  14
%define SYS_GET_PID    15
%define SYS_GET_PPID   16
%define SYS_PS         17

%macro sys_write 2
    mov rax, SYS_WRITE
    lea rdi, [rel %1]
    mov rsi, %2
    int 0x80
%endmacro

%macro sys_exit 0
    mov rax, SYS_EXIT
    int 0x80
%endmacro

%macro sys_putchar_imm 1
    mov rdi, %1
    mov rax, SYS_PUTCHAR
    int 0x80
%endmacro

%macro sys_putchar_mem8 1
    movzx rdi, byte %1
    mov rax, SYS_PUTCHAR
    int 0x80
%endmacro

%macro sys_putchar_reg 1
    movzx rdi, %1
    mov rax, SYS_PUTCHAR
    int 0x80
%endmacro

%macro sys_getchar 0
    mov rax, SYS_GETCHAR
    int 0x80
%endmacro

%macro sys_list_files 0
    mov rax, SYS_LIST_FILES
    int 0x80
%endmacro

%macro sys_cat_reg 1
    mov rdi, %1
    mov rax, SYS_CAT_FILE
    int 0x80
%endmacro

%macro sys_run_reg 1
    mov rdi, %1
    mov rax, SYS_RUN_USER
    int 0x80
%endmacro

%macro sys_version 0
    mov rax, SYS_VERSION
    int 0x80
%endmacro

%macro sys_bootinfo 0
    mov rax, SYS_BOOTINFO
    int 0x80
%endmacro

%macro sys_memstat 0
    mov rax, SYS_MEMSTAT
    int 0x80
%endmacro

%macro sys_rm_reg 1
    mov rdi, %1
    mov rax, SYS_RM_FILE
    int 0x80
%endmacro

%macro sys_uptime 0
    mov rax, SYS_UPTIME
    int 0x80
%endmacro

%macro sys_touch_reg 1
    mov rdi, %1
    mov rax, SYS_TOUCH_FILE
    int 0x80
%endmacro

%macro sys_save_regs 2
    mov rdi, %1
    mov rsi, %2
    mov rax, SYS_SAVE_FILE
    int 0x80
%endmacro

%macro sys_get_pid 0
    mov rax, SYS_GET_PID
    int 0x80
%endmacro

%macro sys_get_ppid 0
    mov rax, SYS_GET_PPID
    int 0x80
%endmacro

%macro sys_ps 0
    mov rax, SYS_PS
    int 0x80
%endmacro

%endif
