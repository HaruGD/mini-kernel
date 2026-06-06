#include "userlib.h"

int main(int argc, char** argv) {
    user_puts("=== ULS_C.ELF ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        user_printf("Listing files at %s from C userland.\n", argv[1]);
        if (user_list_files_at(argv[1]) < 0) {
            user_printf("ls failed: %s\n", argv[1]);
            return 1;
        }
    } else {
        user_puts("Listing files from C userland.");
        user_list_files();
    }
    return 0;
}
