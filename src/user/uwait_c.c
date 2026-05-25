#include "userlib.h"

int user_main(void) {
    static const char banner[] =
        "=== UWAIT_C.ELF ===\n"
        "Waiting for an unreaped child from C userland.\n";

    user_write_cstr(banner);
    return (int)user_wait();
}
