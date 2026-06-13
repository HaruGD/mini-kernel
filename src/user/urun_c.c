#include "userlib.h"

int main(void) {
    static const char banner[] =
        "=== urun_c.elf ===\n"
        "Running uhello_c.elf from C userland.\n";
    static const char target[] = "uhello_c.elf";
    long result;

    user_write_cstr(banner);
    result = user_run(target);
    if (result < 0) {
        user_write_cstr("run failed.\n");
        return 1;
    }

    return (int)user_laststatus();
}
