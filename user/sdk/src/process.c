#include <os64/os64.h>
#include "internal.h"

void os_exit(int code) {
    os_syscall1(OS_SYS_EXIT, code);
    for (;;) {
    }
}

long os_getpid(void) {
    return os_syscall0(OS_SYS_GETPID);
}

long os_getppid(void) {
    return os_syscall0(OS_SYS_GETPPID);
}

long os_run(const char* command) {
    return os_syscall1(OS_SYS_RUN, (long)command);
}

long os_wait(void) {
    return os_syscall0(OS_SYS_WAIT);
}

long os_yield(void) {
    return os_syscall0(OS_SYS_YIELD);
}

long os_sleep(uint32_t ticks) {
    return os_syscall1(OS_SYS_SLEEP, ticks);
}

long os_uptime(void) {
    return os_syscall0(OS_SYS_UPTIME);
}

long os_reap_children(void) {
    return os_syscall0(OS_SYS_REAP);
}
