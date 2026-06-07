#include "userlib.h"

#define URM_INPUT_MAX 64
#define URM_PATH_MAX 160

int main(int argc, char** argv) {
    char input[URM_INPUT_MAX];
    char path_buffer[URM_PATH_MAX];
    char* file_name;

    user_puts("=== URM_C.ELF ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        file_name = argv[1];
    } else {
        user_puts("Enter a file name to delete.");
        user_write_cstr("file> ");
        user_read_line(input, sizeof(input));
        file_name = user_trim(input);
    }

    if (file_name[0] == '\0') {
        user_puts("No file name provided.");
        return 1;
    }

    if (!user_resolve_path(file_name, path_buffer, sizeof(path_buffer))) {
        user_puts("Path is invalid or too long.");
        return 1;
    }

    if (user_rm_silent(path_buffer) < 0) {
        user_printf("rm failed: %s\n", file_name);
        return 1;
    }

    user_printf("Deleted: %s\n", path_buffer);
    return 0;
}
