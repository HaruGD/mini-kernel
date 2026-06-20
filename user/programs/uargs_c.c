#include <os64/os64.h>

int main(int argc, char** argv) {
    os_puts("=== uargs_c.elf ===");
    os_printf("argc=%d\n", argc);

    for (int i = 0; i < argc; i++) {
        os_printf("argv[%d]=%s\n", i, argv[i] != 0 ? argv[i] : "(null)");
    }

    return argc;
}
