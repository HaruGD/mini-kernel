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
%define SYS_LAST_STATUS 18
%define SYS_WAIT_CHILD 19
%define SYS_SCHED_INFO 20
%define SYS_YIELD 21
%define SYS_RESUME_USER 22
%define SYS_KILL_USER 23
%define SYS_REAP_ALL_CHILDREN 24
%define SYS_JOBS 25
%define SYS_SLEEP 26
%define SYS_SET_BACKGROUND 27
%define SYS_CHILDREN_ACTIVE 28
%define SYS_REAP_ALL_CHILDREN_SILENT 29
%define SYS_RM_FILE_SILENT 30
%define SYS_TOUCH_FILE_SILENT 31
%define SYS_SAVE_FILE_SILENT 32
%define SYS_VFS_MOUNTS 33
%define SYS_LIST_FILES_AT 34
%define SYS_VFS_OPEN 35
%define SYS_VFS_READ 36
%define SYS_VFS_WRITE 37
%define SYS_VFS_CLOSE 38
%define SYS_VFS_SEEK 39
%define SYS_VFS_TELL 40
%define SYS_MKDIR 41
%define SYS_RMDIR 42
%define SYS_VFS_INFO 43
%define SYS_GETCWD 44
%define SYS_CHDIR 45
%define SYS_VFS_OPENDIR 46
%define SYS_VFS_READDIR 47
%define SYS_VFS_CLOSEDIR 48
%define SYS_MKDIR_SILENT 49
%define SYS_RMDIR_SILENT 50

%macro sys_write 2
    mov rax, SYS_WRITE
    lea rdi, [rel %1]
    mov rsi, %2
    int 0x80
%endmacro

%macro sys_exit 0
    xor edi, edi
    mov rax, SYS_EXIT
    int 0x80
%endmacro

%macro sys_exit_code 1
    mov rdi, %1
    mov rax, SYS_EXIT
    int 0x80
%endmacro

%macro sys_exit_reg 1
    mov rdi, %1
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

%macro sys_laststatus 0
    mov rax, SYS_LAST_STATUS
    int 0x80
%endmacro

%macro sys_wait 0
    mov rax, SYS_WAIT_CHILD
    int 0x80
%endmacro

%macro sys_sched 0
    mov rax, SYS_SCHED_INFO
    int 0x80
%endmacro

%macro sys_yield 0
    mov rax, SYS_YIELD
    int 0x80
%endmacro

%macro sys_resume 0
    xor edi, edi
    mov rax, SYS_RESUME_USER
    int 0x80
%endmacro

%macro sys_resume_reg 1
    mov rdi, %1
    mov rax, SYS_RESUME_USER
    int 0x80
%endmacro

%macro sys_kill_reg 1
    mov rdi, %1
    mov rax, SYS_KILL_USER
    int 0x80
%endmacro

%macro sys_reapall 0
    mov rax, SYS_REAP_ALL_CHILDREN
    int 0x80
%endmacro

%macro sys_jobs 0
    mov rax, SYS_JOBS
    int 0x80
%endmacro

%macro sys_sleep_ticks 1
    mov rdi, %1
    mov rax, SYS_SLEEP
    int 0x80
%endmacro

%endif
