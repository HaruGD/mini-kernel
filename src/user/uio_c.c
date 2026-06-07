#include "userlib.h"

#define UIO_PATH_MAX 64
#define UIO_TEXT_MAX 64
#define UIO_READ_MAX 64
#define UIO_RESOLVED_PATH_MAX 160

static int resolve_uio_path(const char* input, char* output, uint32_t output_size) {
    if (!user_resolve_path(input, output, output_size)) {
        user_puts("Path is invalid or too long.");
        return 0;
    }
    return 1;
}

static void join_args(char* out, uint32_t out_size, int argc, char** argv, int start_index) {
    uint32_t offset = 0;

    if (out_size == 0) {
        return;
    }

    out[0] = '\0';
    for (int i = start_index; i < argc; i++) {
        const char* arg = argv[i] != 0 ? argv[i] : "";
        uint32_t len = (uint32_t)user_strlen(arg);

        if (offset != 0 && offset + 1 < out_size) {
            out[offset++] = ' ';
        }

        for (uint32_t j = 0; j < len && offset + 1 < out_size; j++) {
            out[offset++] = arg[j];
        }

        if (offset + 1 >= out_size) {
            break;
        }
    }

    out[offset] = '\0';
}

static int handle_write_mode(const char* path, const char* text) {
    char read_buffer[UIO_READ_MAX];
    long fd = user_open_file(path, USER_VFS_OPEN_WRITE | USER_VFS_OPEN_CREATE | USER_VFS_OPEN_TRUNCATE);
    if (fd < 0) {
        user_printf("open for write failed: %s\n", path);
        return 1;
    }

    long bytes = user_write_file_handle(fd, text, (uint32_t)user_strlen(text));
    if (bytes < 0) {
        user_puts("write failed.");
        user_close_file(fd);
        return 1;
    }

    long pos = user_tell_file(fd);
    if (pos < 0) {
        user_puts("tell after write failed.");
        user_close_file(fd);
        return 1;
    }

    if (user_close_file(fd) < 0) {
        user_puts("close after write failed.");
        return 1;
    }

    fd = user_open_file(path, USER_VFS_OPEN_READ);
    if (fd < 0) {
        user_printf("open for read failed: %s\n", path);
        return 1;
    }

    bytes = user_read_file_handle(fd, read_buffer, sizeof(read_buffer) - 1);
    if (bytes < 0) {
        user_puts("read failed.");
        user_close_file(fd);
        return 1;
    }

    read_buffer[bytes] = '\0';
    user_printf("Write pos: %u\n", (uint32_t)pos);
    user_printf("Read back: %s\n", read_buffer);
    if (user_close_file(fd) < 0) {
        user_puts("close after read failed.");
        return 1;
    }
    return 0;
}

static int handle_append_mode(const char* path, const char* text) {
    char read_buffer[UIO_READ_MAX];
    long fd = user_open_file(path, USER_VFS_OPEN_WRITE | USER_VFS_OPEN_CREATE | USER_VFS_OPEN_APPEND);
    if (fd < 0) {
        user_printf("open for append failed: %s\n", path);
        return 1;
    }

    long before = user_tell_file(fd);
    if (before < 0) {
        user_puts("tell before append failed.");
        user_close_file(fd);
        return 1;
    }

    long bytes = user_write_file_handle(fd, text, (uint32_t)user_strlen(text));
    if (bytes < 0) {
        user_puts("append failed.");
        user_close_file(fd);
        return 1;
    }

    long after = user_tell_file(fd);
    if (after < 0) {
        user_puts("tell after append failed.");
        user_close_file(fd);
        return 1;
    }

    if (user_close_file(fd) < 0) {
        user_puts("close after append failed.");
        return 1;
    }

    fd = user_open_file(path, USER_VFS_OPEN_READ);
    if (fd < 0) {
        user_printf("open for read failed: %s\n", path);
        return 1;
    }

    bytes = user_read_file_handle(fd, read_buffer, sizeof(read_buffer) - 1);
    if (bytes < 0) {
        user_puts("read after append failed.");
        user_close_file(fd);
        return 1;
    }

    read_buffer[bytes] = '\0';
    user_printf("Append pos: %u -> %u\n", (uint32_t)before, (uint32_t)after);
    user_printf("Read back: %s\n", read_buffer);
    if (user_close_file(fd) < 0) {
        user_puts("close after append read failed.");
        return 1;
    }
    return 0;
}

static int handle_seek_mode(const char* path, const char* offset_text) {
    char read_buffer[UIO_READ_MAX];
    uint32_t offset = 0;
    if (!user_parse_u32_strict(offset_text, &offset)) {
        user_printf("invalid seek offset: %s\n", offset_text);
        return 1;
    }

    long fd = user_open_file(path, USER_VFS_OPEN_READ);
    if (fd < 0) {
        user_printf("open for seek failed: %s\n", path);
        return 1;
    }

    long new_pos = user_seek_file(fd, (int32_t)offset, USER_VFS_SEEK_SET);
    if (new_pos < 0) {
        user_puts("seek failed.");
        user_close_file(fd);
        return 1;
    }

    long bytes = user_read_file_handle(fd, read_buffer, sizeof(read_buffer) - 1);
    if (bytes < 0) {
        user_puts("read after seek failed.");
        user_close_file(fd);
        return 1;
    }

    read_buffer[bytes] = '\0';
    user_printf("Seek pos: %u\n", (uint32_t)new_pos);
    user_printf("Read from seek: %s\n", read_buffer);
    if (user_close_file(fd) < 0) {
        user_puts("close after seek failed.");
        return 1;
    }
    return 0;
}

static int handle_leak_mode(const char* path, const char* text) {
    long fd = user_open_file(path, USER_VFS_OPEN_WRITE | USER_VFS_OPEN_CREATE | USER_VFS_OPEN_TRUNCATE);
    if (fd < 0) {
        user_printf("open for leak failed: %s\n", path);
        return 1;
    }

    long bytes = user_write_file_handle(fd, text, (uint32_t)user_strlen(text));
    if (bytes < 0) {
        user_puts("write in leak mode failed.");
        return 1;
    }

    user_printf("Leaked handle fd=%d after writing %u byte(s).\n", (int32_t)fd, (uint32_t)bytes);
    user_puts("Exiting without close to test per-process VFS cleanup.");
    return 0;
}

int main(int argc, char** argv) {
    char path_input[UIO_PATH_MAX];
    char text_input[UIO_TEXT_MAX];
    char joined_text[UIO_TEXT_MAX];
    char resolved_path[UIO_RESOLVED_PATH_MAX];
    char* path;
    char* text;

    user_puts("=== UIO_C.ELF ===");

    if (argc >= 2 && argv[1] != 0 && user_str_eq(argv[1], "append")) {
        if (argc < 4 || argv[2] == 0 || argv[2][0] == '\0' || argv[3] == 0) {
            user_puts("Usage: uio append [file] [text]");
            return 1;
        }
        join_args(joined_text, sizeof(joined_text), argc, argv, 3);
        if (!resolve_uio_path(argv[2], resolved_path, sizeof(resolved_path))) {
            return 1;
        }
        return handle_append_mode(resolved_path, joined_text);
    }

    if (argc >= 2 && argv[1] != 0 && user_str_eq(argv[1], "seek")) {
        if (argc < 4 || argv[2] == 0 || argv[2][0] == '\0' || argv[3] == 0 || argv[3][0] == '\0') {
            user_puts("Usage: uio seek [file] [offset]");
            return 1;
        }
        if (!resolve_uio_path(argv[2], resolved_path, sizeof(resolved_path))) {
            return 1;
        }
        return handle_seek_mode(resolved_path, argv[3]);
    }

    if (argc >= 2 && argv[1] != 0 && user_str_eq(argv[1], "leak")) {
        if (argc < 4 || argv[2] == 0 || argv[2][0] == '\0' || argv[3] == 0) {
            user_puts("Usage: uio leak [file] [text]");
            return 1;
        }
        join_args(joined_text, sizeof(joined_text), argc, argv, 3);
        if (!resolve_uio_path(argv[2], resolved_path, sizeof(resolved_path))) {
            return 1;
        }
        return handle_leak_mode(resolved_path, joined_text);
    }

    if (argc >= 3 && argv[1] != 0 && argv[2] != 0) {
        path = argv[1];
        join_args(joined_text, sizeof(joined_text), argc, argv, 2);
        text = joined_text;
        if (!resolve_uio_path(path, resolved_path, sizeof(resolved_path))) {
            return 1;
        }
        return handle_write_mode(resolved_path, text);
    }

    user_puts("Open, write, close, reopen, and read back using VFS handles.");
    user_puts("Modes: uio [file] [text], uio append [file] [text], uio seek [file] [offset], uio leak [file] [text]");

    user_write_cstr("file> ");
    user_read_line(path_input, sizeof(path_input));
    path = user_trim(path_input);
    if (path[0] == '\0') {
        user_puts("No file name provided.");
        return 1;
    }

    user_write_cstr("text> ");
    user_read_line(text_input, sizeof(text_input));
    text = user_trim(text_input);
    if (!resolve_uio_path(path, resolved_path, sizeof(resolved_path))) {
        return 1;
    }
    return handle_write_mode(resolved_path, text);
}
