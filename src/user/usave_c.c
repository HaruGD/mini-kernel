#include "userlib.h"

#define USAVE_FILE_MAX 64
#define USAVE_TEXT_MAX 64
#define USAVE_PATH_MAX 160

int main(int argc, char** argv) {
    char file_input[USAVE_FILE_MAX];
    char text_input[USAVE_TEXT_MAX];
    char path_buffer[USAVE_PATH_MAX];
    char* file_name;
    char* text;
    uint32_t offset = 0;

    user_puts("=== USAVE_C.ELF ===");
    if (argc >= 3 && argv[1] != 0 && argv[2] != 0) {
        file_name = argv[1];
        text_input[0] = '\0';
        for (int i = 2; i < argc; i++) {
            const char* arg = argv[i] != 0 ? argv[i] : "";
            uint32_t len = (uint32_t)user_strlen(arg);
            if (offset != 0 && offset + 1 < sizeof(text_input)) {
                text_input[offset++] = ' ';
            }
            for (uint32_t j = 0; j < len && offset + 1 < sizeof(text_input); j++) {
                text_input[offset++] = arg[j];
            }
            if (offset + 1 >= sizeof(text_input)) {
                break;
            }
        }
        text_input[offset] = '\0';
        text = text_input;
    } else {
        user_puts("Enter a file name and one line of text to save.");

        user_write_cstr("file> ");
        user_read_line(file_input, sizeof(file_input));
        file_name = user_trim(file_input);
        if (file_name[0] == '\0') {
            user_puts("No file name provided.");
            return 1;
        }

        user_write_cstr("text> ");
        user_read_line(text_input, sizeof(text_input));
        text = user_trim(text_input);
        if (text[0] == '\0') {
            user_puts("No text provided.");
            return 1;
        }
    }

    if (!user_resolve_path(file_name, path_buffer, sizeof(path_buffer))) {
        user_puts("Path is invalid or too long.");
        return 1;
    }

    if (user_save_silent(path_buffer, text) < 0) {
        user_printf("save failed: %s\n", file_name);
        return 1;
    }

    user_printf("Saved to %s\n", path_buffer);
    return 0;
}
