static inline int user_normalize_path(const char* cwd, const char* input, char* out, uint32_t capacity);

static inline int user_resolve_path(const char* input, char* out, uint32_t capacity) {
    char cwd[160];
    if (input == 0 || out == 0 || capacity == 0) {
        return 0;
    }
    if (user_getcwd(cwd, sizeof(cwd)) < 0) {
        cwd[0] = '/';
        cwd[1] = '\0';
    }
    return user_normalize_path(cwd, input, out, capacity);
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

static inline void user_puts(const char* text) {
    user_write_cstr(text);
    user_write_cstr("\n");
}

static inline void user_print_hex32(uint32_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];

    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buffer[i] = hex_chars[value & 0x0F];
        value >>= 4;
    }
    buffer[10] = '\0';
    user_write_cstr(buffer);
}

static inline void user_print_u32(uint32_t value) {
    char buffer[11];
    uint32_t index = 0;

    if (value == 0) {
        user_putchar('0');
        return;
    }

    while (value > 0) {
        buffer[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (index > 0) {
        user_putchar(buffer[--index]);
    }
}

static inline void user_print_i32(int32_t value) {
    if (value < 0) {
        user_putchar('-');
        user_print_u32((uint32_t)(-(int64_t)value));
        return;
    }
    user_print_u32((uint32_t)value);
}

static inline void user_vprintf(const char* format, va_list args) {
    while (*format != '\0') {
        if (*format != '%') {
            user_putchar(*format++);
            continue;
        }

        format++;
        if (*format == '\0') {
            break;
        }

        switch (*format) {
            case '%':
                user_putchar('%');
                break;
            case 'c':
                user_putchar((char)va_arg(args, int));
                break;
            case 's': {
                const char* text = va_arg(args, const char*);
                user_write_cstr(text != 0 ? text : "(null)");
                break;
            }
            case 'd':
                user_print_i32(va_arg(args, int));
                break;
            case 'u':
                user_print_u32(va_arg(args, uint32_t));
                break;
            case 'x':
                user_print_hex32(va_arg(args, uint32_t));
                break;
            default:
                user_putchar('%');
                user_putchar(*format);
                break;
        }
        format++;
    }
}

static inline void user_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    user_vprintf(format, args);
    va_end(args);
}

static inline int user_str_eq(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static inline int user_str_startswith(const char* text, const char* prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

static inline int user_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static inline char* user_skip_spaces(char* text) {
    while (*text != '\0' && user_is_space(*text)) {
        text++;
    }
    return text;
}

static inline const char* user_skip_spaces_const(const char* text) {
    while (*text != '\0' && user_is_space(*text)) {
        text++;
    }
    return text;
}

static inline void user_trim_right(char* text) {
    uint64_t len = user_strlen(text);
    while (len > 0 && user_is_space(text[len - 1])) {
        text[--len] = '\0';
    }
}

static inline char* user_trim(char* text) {
    char* start = user_skip_spaces(text);
    user_trim_right(start);
    return start;
}

static inline char* user_split_token(char* text) {
    while (*text != '\0' && !user_is_space(*text)) {
        text++;
    }
    if (*text == '\0') {
        return 0;
    }
    *text = '\0';
    return user_skip_spaces(text + 1);
}

static inline char* user_find_char(char* text, char ch) {
    while (*text != '\0') {
        if (*text == ch) {
            return text;
        }
        text++;
    }
    return 0;
}

static inline uint32_t user_parse_u32(const char* text) {
    uint32_t value = 0;
    uint32_t base = 10;
    char ch;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }

    while ((ch = *text) != '\0') {
        uint32_t digit;

        if (ch >= '0' && ch <= '9') {
            digit = (uint32_t)(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = (uint32_t)(ch - 'a' + 10);
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = (uint32_t)(ch - 'A' + 10);
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        value = (value * base) + digit;
        text++;
    }
    return value;
}

static inline int user_parse_u32_strict(const char* text, uint32_t* value_out) {
    uint32_t value = 0;
    uint32_t base = 10;
    uint32_t saw_digit = 0;
    char ch;

    text = user_skip_spaces_const(text);
    if (*text == '\0') {
        return 0;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }

    while ((ch = *text) != '\0') {
        uint32_t digit;

        if (ch >= '0' && ch <= '9') {
            digit = (uint32_t)(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = (uint32_t)(ch - 'a' + 10);
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = (uint32_t)(ch - 'A' + 10);
        } else if (user_is_space(ch)) {
            break;
        } else {
            return 0;
        }

        if (digit >= base) {
            return 0;
        }

        saw_digit = 1;
        value = (value * base) + digit;
        text++;
    }

    text = user_skip_spaces_const(text);
    if (*text != '\0' || !saw_digit) {
        return 0;
    }

    if (value_out != 0) {
        *value_out = value;
    }
    return 1;
}
