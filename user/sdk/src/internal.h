#ifndef OS64_SDK_INTERNAL_H
#define OS64_SDK_INTERNAL_H

enum {
    OS_SYS_EXIT = 2,
    OS_SYS_PUTCHAR = 3,
    OS_SYS_GETCHAR = 4,
    OS_SYS_RUN = 7,
    OS_SYS_UPTIME = 12,
    OS_SYS_GETPID = 15,
    OS_SYS_GETPPID = 16,
    OS_SYS_WAIT = 19,
    OS_SYS_YIELD = 21,
    OS_SYS_SLEEP = 26,
    OS_SYS_REAP = 29,
    OS_SYS_REMOVE = 30,
    OS_SYS_TOUCH = 31,
    OS_SYS_OPEN = 35,
    OS_SYS_READ = 36,
    OS_SYS_WRITE_FD = 37,
    OS_SYS_CLOSE = 38,
    OS_SYS_SEEK = 39,
    OS_SYS_TELL = 40,
    OS_SYS_STAT = 43,
    OS_SYS_GETCWD = 44,
    OS_SYS_CHDIR = 45,
    OS_SYS_OPENDIR = 46,
    OS_SYS_READDIR = 47,
    OS_SYS_CLOSEDIR = 48,
    OS_SYS_MKDIR = 49,
    OS_SYS_RMDIR = 50,
    OS_SYS_RENAME = 51,
    OS_SYS_CLEAR = 52,
    OS_SYS_BRK = 53
};

long os_syscall0(long number);
long os_syscall1(long number, long arg1);
long os_syscall2(long number, long arg1, long arg2);
long os_syscall3(long number, long arg1, long arg2, long arg3);

#endif
