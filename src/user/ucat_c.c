#include "userlib.h"

#define UCAT_INPUT_MAX 64
#define UCAT_PATH_MAX 160

int main(int argc, char** argv) {
    char input[UCAT_INPUT_MAX];
    char path_buffer[UCAT_PATH_MAX];
    char* file_name;

    user_puts("=== ucat_c.elf ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        file_name = argv[1];
    } else {
        user_puts("Enter a file name to print.");
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

    if (user_cat(path_buffer) < 0) {
        user_printf("cat failed: %s\n", file_name);
        return 1;
    }
    return 0;
}
