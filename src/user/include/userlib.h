#ifndef USERLIB_H
#define USERLIB_H

#include <stdarg.h>
#include <stdint.h>

#define USER_VFS_OPEN_READ     0x00000001u
#define USER_VFS_OPEN_WRITE    0x00000002u
#define USER_VFS_OPEN_CREATE   0x00000004u
#define USER_VFS_OPEN_TRUNCATE 0x00000008u
#define USER_VFS_OPEN_APPEND   0x00000010u

#define USER_VFS_SEEK_SET 0u
#define USER_VFS_SEEK_CUR 1u
#define USER_VFS_SEEK_END 2u

#define USER_VFS_NODE_NONE 0u
#define USER_VFS_NODE_FILE 1u
#define USER_VFS_NODE_DIR  2u

typedef struct UserVFSInfo {
    uint32_t type;
    uint32_t size;
} UserVFSInfo;

typedef struct UserDirEntry {
    uint32_t type;
    uint32_t size;
    char name[64];
} UserDirEntry;

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

static inline long user_syscall3(long number, long arg1, long arg2, long arg3) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
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

static inline long user_version(void) {
    return user_syscall0(8);
}

static inline long user_bootinfo(void) {
    return user_syscall0(9);
}

static inline long user_memstat(void) {
    return user_syscall0(10);
}

static inline long user_rm(const char* filename) {
    return user_syscall1(11, (long)filename);
}

static inline long user_uptime(void) {
    return user_syscall0(12);
}

static inline long user_touch(const char* filename) {
    return user_syscall1(13, (long)filename);
}

static inline long user_save(const char* filename, const char* text) {
    return user_syscall2(14, (long)filename, (long)text);
}

static inline long user_list_files(void) {
    return user_syscall0(5);
}

static inline long user_list_files_at(const char* path) {
    return user_syscall1(34, (long)path);
}

static inline long user_cat(const char* filename) {
    return user_syscall1(6, (long)filename);
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

static inline long user_sched(void) {
    return user_syscall0(20);
}

static inline long user_yield(void) {
    return user_syscall0(21);
}

static inline long user_resume(long pid) {
    return user_syscall1(22, pid);
}

static inline long user_kill(long pid) {
    return user_syscall1(23, pid);
}

static inline long user_reapall(void) {
    return user_syscall0(24);
}

static inline long user_jobs(void) {
    return user_syscall0(25);
}

static inline long user_sleep(uint32_t ticks) {
    return user_syscall1(26, (long)ticks);
}

static inline long user_set_background(long pid, long enabled) {
    return user_syscall2(27, pid, enabled);
}

static inline long user_children_active(void) {
    return user_syscall0(28);
}

static inline long user_reapall_silent(void) {
    return user_syscall0(29);
}

static inline long user_rm_silent(const char* filename) {
    return user_syscall1(30, (long)filename);
}

static inline long user_touch_silent(const char* filename) {
    return user_syscall1(31, (long)filename);
}

static inline long user_save_silent(const char* filename, const char* text) {
    return user_syscall2(32, (long)filename, (long)text);
}

static inline long user_mounts(void) {
    return user_syscall0(33);
}

static inline long user_open_file(const char* path, uint32_t mode) {
    return user_syscall2(35, (long)path, (long)mode);
}

static inline long user_read_file_handle(long fd, void* buffer, uint32_t size) {
    return user_syscall3(36, fd, (long)buffer, (long)size);
}

static inline long user_write_file_handle(long fd, const void* buffer, uint32_t size) {
    return user_syscall3(37, fd, (long)buffer, (long)size);
}

static inline long user_close_file(long fd) {
    return user_syscall1(38, fd);
}

static inline long user_seek_file(long fd, int32_t offset, uint32_t whence) {
    return user_syscall3(39, fd, (long)offset, (long)whence);
}

static inline long user_tell_file(long fd) {
    return user_syscall1(40, fd);
}

static inline long user_mkdir(const char* path) {
    return user_syscall1(41, (long)path);
}

static inline long user_rmdir(const char* path) {
    return user_syscall1(42, (long)path);
}

static inline long user_mkdir_silent(const char* path) {
    return user_syscall1(49, (long)path);
}

static inline long user_rmdir_silent(const char* path) {
    return user_syscall1(50, (long)path);
}

static inline long user_rename(const char* old_path, const char* new_path) {
    return user_syscall2(51, (long)old_path, (long)new_path);
}

static inline long user_get_file_info(const char* path, UserVFSInfo* info) {
    return user_syscall2(43, (long)path, (long)info);
}

static inline long user_getcwd(char* buffer, uint32_t capacity) {
    return user_syscall2(44, (long)buffer, (long)capacity);
}

static inline long user_chdir(const char* path) {
    return user_syscall1(45, (long)path);
}

static inline long user_opendir(const char* path) {
    return user_syscall1(46, (long)path);
}

static inline long user_readdir(long fd, UserDirEntry* entry) {
    return user_syscall2(47, fd, (long)entry);
}

static inline long user_closedir(long fd) {
    return user_syscall1(48, fd);
}

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

static inline int user_normalize_path(const char* cwd, const char* input, char* out, uint32_t capacity) {
    char segments[16][32];
    uint32_t segment_count = 0;
    const char* cursor;

    if (out == 0 || capacity < 2 || input == 0 || input[0] == '\0') {
        return 0;
    }

    out[0] = '\0';

    if (input[0] == '/') {
        cursor = input;
    } else {
        cursor = cwd != 0 && cwd[0] != '\0' ? cwd : "/";
        while (*cursor != '\0') {
            while (*cursor == '/') {
                cursor++;
            }
            if (*cursor == '\0') {
                break;
            }

            uint32_t len = 0;
            while (cursor[len] != '\0' && cursor[len] != '/') {
                if (len + 1 >= sizeof(segments[0])) {
                    return 0;
                }
                segments[segment_count][len] = cursor[len];
                len++;
            }
            segments[segment_count][len] = '\0';
            if (segment_count + 1 >= 16) {
                return 0;
            }
            segment_count++;
            cursor += len;
        }
        cursor = input;
    }

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        char segment[32];
        uint32_t len = 0;
        while (cursor[len] != '\0' && cursor[len] != '/') {
            if (len + 1 >= sizeof(segment)) {
                return 0;
            }
            segment[len] = cursor[len];
            len++;
        }
        segment[len] = '\0';

        if (user_str_eq(segment, ".")) {
        } else if (user_str_eq(segment, "..")) {
            if (segment_count > 0) {
                segment_count--;
            }
        } else {
            if (segment_count >= 16) {
                return 0;
            }
            for (uint32_t i = 0; i <= len; i++) {
                segments[segment_count][i] = segment[i];
            }
            segment_count++;
        }

        cursor += len;
    }

    {
        uint32_t offset = 0;
        out[offset++] = '/';

        for (uint32_t i = 0; i < segment_count; i++) {
            uint32_t len = (uint32_t)user_strlen(segments[i]);
            if (offset + len + (i + 1 < segment_count ? 1u : 0u) + 1u > capacity) {
                return 0;
            }
            for (uint32_t j = 0; j < len; j++) {
                out[offset++] = segments[i][j];
            }
            if (i + 1 < segment_count) {
                out[offset++] = '/';
            }
        }
        out[offset] = '\0';
    }

    return 1;
}

static inline void user_read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;

    if (capacity == 0) {
        return;
    }

    while (1) {
        long ch = user_getchar();
        if (ch < 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            user_putchar('\n');
            break;
        }

        if (ch == '\b') {
            if (length > 0) {
                length--;
                buffer[length] = '\0';
                user_putchar('\b');
                user_putchar(' ');
                user_putchar('\b');
            }
            continue;
        }

        if (ch < 32 || ch > 126) {
            continue;
        }

        if (length + 1 >= capacity) {
            continue;
        }

        buffer[length++] = (char)ch;
        buffer[length] = '\0';
        user_putchar((char)ch);
    }

    buffer[length] = '\0';
}

#endif
