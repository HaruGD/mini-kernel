#include "userlib.h"

int main(void) {
    static const char banner[] =
        "=== UWAIT_C.ELF ===\n"
        "Waiting for an unreaped child of the current process.\n";

    user_write_cstr(banner);
    return (int)user_wait();
}
