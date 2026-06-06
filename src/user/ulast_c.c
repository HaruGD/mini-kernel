#include "userlib.h"

int main(void) {
    static const char banner[] =
        "=== ULAST_C.ELF ===\n"
        "Showing the current process child status from C userland.\n";

    user_write_cstr(banner);
    return (int)user_laststatus();
}
