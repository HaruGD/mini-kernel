#include <stdint.h>

extern "C" {
    #include "arch/x86_64/io.h"
}

#include "drivers/terminal.h"
#include "kernel/klog.h"
#include "kernel/kutil64.h"
#include "kernel/cpu_local.h"

extern Terminal terminal;
static volatile uint32_t console_output_lock = 0;
#define CONSOLE_LOCK_BYPASSED (1ULL << 63)

static uint64_t console_lock_acquire() {
    uint64_t flags = 0;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local) && local->emergency_active) {
        if (__atomic_exchange_n(&console_output_lock,
                                1u,
                                __ATOMIC_ACQUIRE) != 0) {
            return flags | CONSOLE_LOCK_BYPASSED;
        }
        return flags;
    }
    while (__atomic_exchange_n(&console_output_lock,
                               1u,
                               __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile("pause");
    }
    return flags;
}

static void console_lock_release(uint64_t flags) {
    if ((flags & CONSOLE_LOCK_BYPASSED) == 0) {
        __atomic_store_n(&console_output_lock, 0u, __ATOMIC_RELEASE);
    }
    if ((flags & (1ULL << 9)) != 0) {
        __asm__ volatile("sti" : : : "memory");
    }
}

int strlen64(const char* str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp64(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return ((unsigned char)*a) - ((unsigned char)*b);
}

void copy_string64(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

int is_space64(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void serial_init() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

int serial_ready() {
    return inb(0x3F8 + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_ready()) {
    }
    outb(0x3F8, (unsigned char)c);
}

void putchar_both(char c) {
    klog_capture_char(c);
    terminal.putchar(c);
    if (c == '\n') {
        serial_putchar('\r');
    }
    serial_putchar(c);
}

void print(const char* str) {
    const uint64_t flags = console_lock_acquire();
    for (int i = 0; str[i] != '\0'; i++) {
        putchar_both(str[i]);
    }
    console_lock_release(flags);
}

void print_n(const char* str, uint64_t len) {
    const uint64_t flags = console_lock_acquire();
    for (uint64_t i = 0; i < len; i++) {
        putchar_both(str[i]);
    }
    console_lock_release(flags);
}

void print_hex32(uint32_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buffer[i] = hex_chars[value & 0x0F];
        value >>= 4;
    }
    buffer[10] = '\0';
    print(buffer);
}

void print_hex64(uint64_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 17; i >= 2; i--) {
        buffer[i] = hex_chars[(uint32_t)(value & 0x0F)];
        value >>= 4;
    }
    buffer[18] = '\0';
    print(buffer);
}

extern "C" void debug_print64(const char* str) {
    print(str);
}

extern "C" void debug_print_hex64(uint32_t value) {
    print_hex32(value);
}

extern "C" void debug_print_hex64_u64(uint64_t value) {
    print_hex64(value);
}

uint64_t read_tsc() {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
