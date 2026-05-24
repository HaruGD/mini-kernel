%ifndef USER_SYSCALL_INC
%define USER_SYSCALL_INC

%define SYS_WRITE   1
%define SYS_EXIT    2
%define SYS_PUTCHAR 3
%define SYS_GETCHAR 4
%define SYS_LIST_FILES 5
%define SYS_CAT_FILE   6

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

%endif
