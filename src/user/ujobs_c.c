#include "userlib.h"

int main(void) {
    static const char banner[] =
        "=== UJOBS_C.ELF ===\n"
        "Listing jobs from C userland.\n";

    user_write_cstr(banner);
    user_jobs();
    return 0;
}
