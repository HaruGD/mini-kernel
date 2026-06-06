#include "userlib.h"

int main(void) {
    long ch;

    user_puts("=== UINFO_C.ELF ===");
    user_puts("This is the C version of UINFO.");
    user_write_cstr("Press one key to return: ");

    ch = user_getchar();
    if (ch < 0) {
        user_puts("\ngetchar failed.");
        return 1;
    }

    user_printf("You chose: %c\n", (char)ch);

    return (int)(unsigned char)ch;
}
