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

long os_get_process_identity(OsProcessIdentity* identity) {
    return os_syscall1(OS_SYS_GET_PROCESS_IDENTITY, (long)identity);
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

long os_kill(uint32_t pid) {
    return os_syscall1(OS_SYS_KILL, pid);
}

long os_set_background(uint32_t pid, uint32_t enabled) {
    return os_syscall2(OS_SYS_SET_BACKGROUND, pid, enabled);
}

long os_children_active(void) {
    return os_syscall0(OS_SYS_CHILDREN_ACTIVE);
}
