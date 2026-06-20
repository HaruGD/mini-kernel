#include "internal.h"

long os_syscall0(long number) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number)
        : "memory");
    return result;
}

long os_syscall1(long number, long arg1) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1)
        : "memory");
    return result;
}

long os_syscall2(long number, long arg1, long arg2) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "memory");
    return result;
}

long os_syscall3(long number, long arg1, long arg2, long arg3) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory");
    return result;
}
