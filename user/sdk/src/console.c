#include <os64/os64.h>
#include "internal.h"

long os_putchar(char ch) {
    return os_syscall1(OS_SYS_PUTCHAR, (long)(unsigned char)ch);
}

long os_getchar(void) {
    return os_syscall0(OS_SYS_GETCHAR);
}

long os_clear(void) {
    return os_syscall0(OS_SYS_CLEAR);
}

long os_write(const char* text, size_t length) {
    size_t written = 0;

    if (text == 0) {
        return OS_ERROR;
    }
    while (written < length) {
        if (os_putchar(text[written]) < 0) {
            return OS_ERROR;
        }
        written++;
    }
    return (long)written;
}

long os_puts(const char* text) {
    long result = os_write(text, os_strlen(text));
    if (result < 0 || os_putchar('\n') < 0) {
        return OS_ERROR;
    }
    return result + 1;
}

static void print_unsigned(uint64_t value, uint32_t base, int prefix) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[32];
    size_t count = 0;

    if (prefix != 0) {
        os_write("0x", 2);
    }
    if (value == 0) {
        os_putchar('0');
        return;
    }
    while (value != 0) {
        buffer[count++] = digits[value % base];
        value /= base;
    }
    while (count > 0) {
        os_putchar(buffer[--count]);
    }
}

void os_vprintf(const char* format, va_list args) {
    while (format != 0 && *format != '\0') {
        int wide = 0;

        if (*format != '%') {
            os_putchar(*format++);
            continue;
        }
        format++;
        if (*format == 'l') {
            wide = 1;
            format++;
        }

        switch (*format) {
            case '%':
                os_putchar('%');
                break;
            case 'c':
                os_putchar((char)va_arg(args, int));
                break;
            case 's': {
                const char* text = va_arg(args, const char*);
                if (text == 0) {
                    text = "(null)";
                }
                os_write(text, os_strlen(text));
                break;
            }
            case 'd': {
                int64_t value = wide ? (int64_t)va_arg(args, long) : (int64_t)va_arg(args, int);
                if (value < 0) {
                    os_putchar('-');
                    print_unsigned((uint64_t)(-(value + 1)) + 1u, 10, 0);
                } else {
                    print_unsigned((uint64_t)value, 10, 0);
                }
                break;
            }
            case 'u':
                print_unsigned(wide ? (uint64_t)va_arg(args, unsigned long)
                                    : (uint64_t)va_arg(args, unsigned int), 10, 0);
                break;
            case 'x':
                print_unsigned(wide ? (uint64_t)va_arg(args, unsigned long)
                                    : (uint64_t)va_arg(args, unsigned int), 16, 0);
                break;
            case 'p':
                print_unsigned((uint64_t)(uintptr_t)va_arg(args, void*), 16, 1);
                break;
            case '\0':
                return;
            default:
                os_putchar('%');
                if (wide != 0) {
                    os_putchar('l');
                }
                os_putchar(*format);
                break;
        }
        format++;
    }
}

void os_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    os_vprintf(format, args);
    va_end(args);
}

size_t os_read_line(char* buffer, size_t capacity) {
    size_t length = 0;

    if (buffer == 0 || capacity == 0) {
        return 0;
    }
    buffer[0] = '\0';
    for (;;) {
        long value = os_getchar();
        char ch;

        if (value < 0) {
            continue;
        }
        ch = (char)value;
        if (ch == '\r' || ch == '\n') {
            os_putchar('\n');
            break;
        }
        if (ch == '\b') {
            if (length > 0) {
                buffer[--length] = '\0';
                os_write("\b \b", 3);
            }
            continue;
        }
        if (ch < 32 || ch > 126 || length + 1 >= capacity) {
            continue;
        }
        buffer[length++] = ch;
        buffer[length] = '\0';
        os_putchar(ch);
    }
    return length;
}
