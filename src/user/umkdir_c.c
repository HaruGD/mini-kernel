#include "userlib.h"

#define UMKDIR_INPUT_MAX 64
#define UMKDIR_PATH_MAX 160

int main(int argc, char** argv) {
    char input[UMKDIR_INPUT_MAX];
    char path_buffer[UMKDIR_PATH_MAX];
    char* path;

    user_puts("=== umkdir_c.elf ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        path = argv[1];
    } else {
        user_puts("Enter a directory path to create.");
        user_write_cstr("dir> ");
        user_read_line(input, sizeof(input));
        path = user_trim(input);
    }

    if (path[0] == '\0') {
        user_puts("No directory path provided.");
        return 1;
    }

    if (!user_resolve_path(path, path_buffer, sizeof(path_buffer))) {
        user_puts("Path is invalid or too long.");
        return 1;
    }

    if (user_mkdir_silent(path_buffer) < 0) {
        user_printf("mkdir failed: %s\n", path);
        return 1;
    }

    user_printf("Created dir: %s\n", path_buffer);
    return 0;
}
