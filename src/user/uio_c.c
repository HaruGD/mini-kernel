#include "userlib.h"

#define UIO_PATH_MAX 64
#define UIO_TEXT_MAX 64

int main(void) {
    char path_input[UIO_PATH_MAX];
    char text_input[UIO_TEXT_MAX];
    char read_buffer[UIO_TEXT_MAX];
    char* path;
    char* text;
    long fd;
    long bytes;

    user_puts("=== UIO_C.ELF ===");
    user_puts("Open, write, close, reopen, and read back using VFS handles.");

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

    fd = user_open_file(path, USER_VFS_OPEN_WRITE | USER_VFS_OPEN_CREATE | USER_VFS_OPEN_TRUNCATE);
    if (fd < 0) {
        user_printf("open for write failed: %s\n", path);
        return 1;
    }

    bytes = user_write_file_handle(fd, text, (uint32_t)user_strlen(text));
    if (bytes < 0 || user_close_file(fd) < 0) {
        user_puts("write/close failed.");
        return 1;
    }

    fd = user_open_file(path, USER_VFS_OPEN_READ);
    if (fd < 0) {
        user_printf("open for read failed: %s\n", path);
        return 1;
    }

    bytes = user_read_file_handle(fd, read_buffer, sizeof(read_buffer) - 1);
    if (bytes < 0 || user_close_file(fd) < 0) {
        user_puts("read/close failed.");
        return 1;
    }

    read_buffer[bytes] = '\0';
    user_printf("Read back: %s\n", read_buffer);
    return 0;
}
