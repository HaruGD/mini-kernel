#include <os64/os64.h>
#include <os64/syscall_numbers.h>

static long fast_syscall0(long number) {
    register long result __asm__("rax") = number;
    __asm__ volatile("syscall"
                     : "+a"(result)
                     :
                     : "rcx", "r11", "memory");
    return result;
}

static long fast_syscall1(long number, long arg1) {
    register long result __asm__("rax") = number;
    register long first __asm__("rdi") = arg1;
    __asm__ volatile("syscall"
                     : "+a"(result)
                     : "D"(first)
                     : "rcx", "r11", "memory");
    return result;
}

static long fast_syscall2(long number, long arg1, long arg2) {
    register long result __asm__("rax") = number;
    register long first __asm__("rdi") = arg1;
    register long second __asm__("rsi") = arg2;
    __asm__ volatile("syscall"
                     : "+a"(result)
                     : "D"(first), "S"(second)
                     : "rcx", "r11", "memory");
    return result;
}

static long fast_syscall0_with_df(long number) {
    register long result __asm__("rax") = number;
    __asm__ volatile("std; syscall; cld"
                     : "+a"(result)
                     :
                     : "rcx", "r11", "cc", "memory");
    return result;
}

static void check(int condition, const char* name, uint32_t* failures) {
    if (!condition) {
        (*failures)++;
        os_printf("[P5S-E][FAIL] %s\n", name);
    }
}

int main(void) {
    static const char fast_write_text[] = "[P5S-E] fast write\n";
    uint32_t failures = 0;
    long pid = fast_syscall0(OS_SYS_GETPID);
    check(pid > 0 && pid == os_getpid(), "getpid parity", &failures);
    check(fast_syscall0(OS_SYS_TIME_FREQUENCY) != 0,
          "time frequency", &failures);
    check(fast_syscall0(OS64_SYSCALL_MAX_NUMBER + 1u) == OS_ERR_UNSUPPORTED,
          "unknown call result", &failures);
    check(fast_syscall2(OS_SYS_WRITE,
                        (long)(uintptr_t)fast_write_text,
                        sizeof(fast_write_text) - 1u) ==
              (long)(sizeof(fast_write_text) - 1u),
          "user buffer", &failures);
    check(fast_syscall2(OS_SYS_WRITE,
                        (long)0xFFFFFFFF80000000ULL,
                        4) == OS_ERR_BAD_BUFFER,
          "kernel pointer rejection", &failures);
    check(fast_syscall0(OS_SYS_YIELD) == OS_SUCCESS,
          "yield and resume", &failures);
    check(fast_syscall1(OS_SYS_SLEEP, 1) == OS_SUCCESS,
          "sleep and resume", &failures);
    check(fast_syscall0_with_df(OS_SYS_GETPID) == pid,
          "unsafe flags iret fallback", &failures);

    os_printf("[P5S-E] fast entry failures=%u\n", failures);
    return failures == 0 ? 0 : 1;
}
