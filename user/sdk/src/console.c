#include <os64/os64.h>
#include "internal.h"

#define OS_CONSOLE_BUFFER_SIZE 512u

typedef struct {
    char data[OS_CONSOLE_BUFFER_SIZE];
    size_t length;
} OsConsoleBuffer;

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
    if (text == 0) {
        return OS_ERROR;
    }
    if (length == 0) {
        return 0;
    }
    return os_syscall2(OS_SYS_WRITE, (long)text, (long)length);
}

static int console_buffer_flush(OsConsoleBuffer* output) {
    if (output->length == 0) {
        return 1;
    }
    if (os_write(output->data, output->length) != (long)output->length) {
        output->length = 0;
        return 0;
    }
    output->length = 0;
    return 1;
}

static int console_buffer_putc(OsConsoleBuffer* output, char ch) {
    if (output->length == sizeof(output->data) &&
        !console_buffer_flush(output)) {
        return 0;
    }
    output->data[output->length++] = ch;
    return 1;
}

static int console_buffer_write(OsConsoleBuffer* output,
                                const char* text,
                                size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (!console_buffer_putc(output, text[i])) {
            return 0;
        }
    }
    return 1;
}

long os_puts(const char* text) {
    OsConsoleBuffer output = {{0}, 0};
    size_t length;

    if (text == 0) {
        return OS_ERROR;
    }
    length = os_strlen(text);
    if (!console_buffer_write(&output, text, length) ||
        !console_buffer_putc(&output, '\n') ||
        !console_buffer_flush(&output)) {
        return OS_ERROR;
    }
    return (long)length + 1;
}

static int print_unsigned(OsConsoleBuffer* output,
                          uint64_t value,
                          uint32_t base,
                          int prefix) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[32];
    size_t count = 0;

    if (prefix != 0) {
        if (!console_buffer_write(output, "0x", 2)) {
            return 0;
        }
    }
    if (value == 0) {
        return console_buffer_putc(output, '0');
    }
    while (value != 0) {
        buffer[count++] = digits[value % base];
        value /= base;
    }
    while (count > 0) {
        if (!console_buffer_putc(output, buffer[--count])) {
            return 0;
        }
    }
    return 1;
}

void os_vprintf(const char* format, va_list args) {
    OsConsoleBuffer output = {{0}, 0};

    while (format != 0 && *format != '\0') {
        int wide = 0;

        if (*format != '%') {
            if (!console_buffer_putc(&output, *format++)) {
                return;
            }
            continue;
        }
        format++;
        if (*format == 'l') {
            wide = 1;
            format++;
        }

        switch (*format) {
            case '%':
                if (!console_buffer_putc(&output, '%')) {
                    return;
                }
                break;
            case 'c':
                if (!console_buffer_putc(&output, (char)va_arg(args, int))) {
                    return;
                }
                break;
            case 's': {
                const char* text = va_arg(args, const char*);
                if (text == 0) {
                    text = "(null)";
                }
                if (!console_buffer_write(&output, text, os_strlen(text))) {
                    return;
                }
                break;
            }
            case 'd': {
                int64_t value = wide ? (int64_t)va_arg(args, long) : (int64_t)va_arg(args, int);
                if (value < 0) {
                    if (!console_buffer_putc(&output, '-') ||
                        !print_unsigned(&output,
                                        (uint64_t)(-(value + 1)) + 1u,
                                        10,
                                        0)) {
                        return;
                    }
                } else {
                    if (!print_unsigned(&output, (uint64_t)value, 10, 0)) {
                        return;
                    }
                }
                break;
            }
            case 'u':
                if (!print_unsigned(&output,
                                    wide ? (uint64_t)va_arg(args, unsigned long)
                                         : (uint64_t)va_arg(args, unsigned int),
                                    10,
                                    0)) {
                    return;
                }
                break;
            case 'x':
                if (!print_unsigned(&output,
                                    wide ? (uint64_t)va_arg(args, unsigned long)
                                         : (uint64_t)va_arg(args, unsigned int),
                                    16,
                                    0)) {
                    return;
                }
                break;
            case 'p':
                if (!print_unsigned(&output,
                                    (uint64_t)(uintptr_t)va_arg(args, void*),
                                    16,
                                    1)) {
                    return;
                }
                break;
            case '\0':
                console_buffer_flush(&output);
                return;
            default:
                if (!console_buffer_putc(&output, '%') ||
                    (wide != 0 && !console_buffer_putc(&output, 'l')) ||
                    !console_buffer_putc(&output, *format)) {
                    return;
                }
                break;
        }
        format++;
    }
    console_buffer_flush(&output);
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
