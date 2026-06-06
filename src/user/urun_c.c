#include "userlib.h"

int main(void) {
    static const char banner[] =
        "=== URUN_C.ELF ===\n"
        "Running UHELLO_C.ELF from C userland.\n";
    static const char target[] = "UHELLO_C.ELF";
    long result;

    user_write_cstr(banner);
    result = user_run(target);
    if (result < 0) {
        user_write_cstr("run failed.\n");
        return 1;
    }

    return (int)user_laststatus();
}
