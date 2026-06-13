#include "userlib.h"

#define URMDIR_INPUT_MAX 64
#define URMDIR_PATH_MAX 160

int main(int argc, char** argv) {
    char input[URMDIR_INPUT_MAX];
    char path_buffer[URMDIR_PATH_MAX];
    char* path;

    user_puts("=== urmdir_c.elf ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        path = argv[1];
    } else {
        user_puts("Enter a directory path to remove.");
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

    if (user_rmdir_silent(path_buffer) < 0) {
        user_printf("rmdir failed: %s\n", path);
        return 1;
    }

    user_printf("Removed dir: %s\n", path_buffer);
    return 0;
}
