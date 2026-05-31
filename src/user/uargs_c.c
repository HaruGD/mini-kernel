#include "userlib.h"

int main(int argc, char** argv) {
    user_puts("=== UARGS_C.ELF ===");
    user_printf("argc=%d\n", argc);

    for (int i = 0; i < argc; i++) {
        user_printf("argv[%d]=%s\n", i, argv[i] != 0 ? argv[i] : "(null)");
    }

    return argc;
}
