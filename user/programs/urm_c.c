#include <os64/os64.h>

#define URM_INPUT_MAX 64
#define URM_PATH_MAX 160

int main(int argc, char** argv) {
    char input[URM_INPUT_MAX];
    char path_buffer[URM_PATH_MAX];
    char* file_name;

    os_puts("=== urm_c.elf ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        file_name = argv[1];
    } else {
        os_puts("Enter a file name to delete.");
        os_write("file> ", 6);
        os_read_line(input, sizeof(input));
        file_name = os_trim(input);
    }

    if (file_name[0] == '\0') {
        os_puts("No file name provided.");
        return 1;
    }

    if (!os_resolve_path(file_name, path_buffer, sizeof(path_buffer))) {
        os_puts("Path is invalid or too long.");
        return 1;
    }

    if (os_remove(path_buffer) < 0) {
        os_printf("rm failed: %s\n", file_name);
        return 1;
    }

    os_printf("Deleted: %s\n", path_buffer);
    return 0;
}
