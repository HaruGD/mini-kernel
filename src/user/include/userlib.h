#ifndef USERLIB_H
#define USERLIB_H

#include <stdint.h>

static inline long user_syscall0(long number) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number)
        : "memory");
    return result;
}

static inline long user_syscall1(long number, long arg1) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1)
        : "memory");
    return result;
}

static inline long user_syscall2(long number, long arg1, long arg2) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "memory");
    return result;
}

static inline long user_write(const char* text, uint64_t length) {
    return user_syscall2(1, (long)text, (long)length);
}

static inline void user_exit(int code) {
    user_syscall1(2, (long)code);
    for (;;) {
    }
}

static inline long user_putchar(char ch) {
    return user_syscall1(3, (long)(unsigned char)ch);
}

static inline long user_getchar(void) {
    return user_syscall0(4);
}

static inline long user_get_pid(void) {
    return user_syscall0(15);
}

static inline long user_get_ppid(void) {
    return user_syscall0(16);
}

static inline long user_run(const char* filename) {
    return user_syscall1(7, (long)filename);
}

static inline long user_list_files(void) {
    return user_syscall0(5);
}

static inline long user_ps(void) {
    return user_syscall0(17);
}

static inline long user_laststatus(void) {
    return user_syscall0(18);
}

static inline long user_wait(void) {
    return user_syscall0(19);
}

static inline long user_jobs(void) {
    return user_syscall0(25);
}

static inline long user_sleep(uint32_t ticks) {
    return user_syscall1(26, (long)ticks);
}

static inline uint64_t user_strlen(const char* text) {
    uint64_t len = 0;
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static inline long user_write_cstr(const char* text) {
    return user_write(text, user_strlen(text));
}

#endif
