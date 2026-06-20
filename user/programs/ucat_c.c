#include <os64/os64.h>

#define UCAT_INPUT_MAX 64
#define UCAT_PATH_MAX 160

int main(int argc, char** argv) {
    char input[UCAT_INPUT_MAX];
    char path_buffer[UCAT_PATH_MAX];
    char* file_name;

    os_puts("=== ucat_c.elf ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        file_name = argv[1];
    } else {
        os_puts("Enter a file name to print.");
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

    uint32_t size = 0;
    char* text = os_read_text_file_alloc(path_buffer, &size);
    if (text == 0) {
        os_printf("cat failed: %s\n", file_name);
        return 1;
    }

    os_write(text, size);
    if (size == 0 || text[size - 1] != '\n') {
        os_putchar('\n');
    }
    os_free(text);
    return 0;
}
