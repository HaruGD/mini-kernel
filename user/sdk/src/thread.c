#include <os64/os64.h>

#include "internal.h"

__attribute__((naked, noreturn))
static void os_thread_return_trampoline(void) {
    __asm__ volatile(
        "mov %rax, %rdi\n"
        "mov $91, %rax\n"
        "int $0x80\n"
        "1: jmp 1b\n");
}

long os_thread_create(OsThreadEntry entry,
                      void* argument,
                      uint32_t stack_size,
                      OsThreadIdentity* identity) {
    if (entry == 0 || identity == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    if (stack_size == 0) {
        stack_size = OS_THREAD_STACK_DEFAULT;
    }
    OsThreadCreateRequest request;
    request.size = sizeof(request);
    request.flags = OS_THREAD_CREATE_FLAG_NONE;
    request.stack_size = stack_size;
    request.reserved = 0;
    request.entry = (uint64_t)(uintptr_t)entry;
    request.argument = (uint64_t)(uintptr_t)argument;
    request.return_trampoline =
        (uint64_t)(uintptr_t)os_thread_return_trampoline;
    return os_syscall2(OS_SYS_THREAD_CREATE, (long)&request, (long)identity);
}

long os_thread_self(OsThreadIdentity* identity) {
    if (identity == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall1(OS_SYS_THREAD_SELF, (long)identity);
}

void os_thread_exit(long code) {
    os_syscall1(OS_SYS_THREAD_EXIT, code);
    for (;;) {
    }
}

long os_thread_join(OsThreadIdentity identity, uint32_t* status) {
    if (identity.tid == 0 || identity.generation == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall3(OS_SYS_THREAD_JOIN,
                       identity.tid,
                       identity.generation,
                       (long)status);
}

long os_thread_yield(void) {
    return os_yield();
}

long os_thread_sleep(uint32_t ticks) {
    return os_sleep(ticks);
}
