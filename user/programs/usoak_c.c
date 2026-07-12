#include <os64/os64.h>

int main(void) {
    for (uint32_t i = 0; i < 20; i++) {
        if (os_yield() < 0 || os_sleep(1) < 0) {
            os_puts("[usoak] scheduler operation failed");
            return 1;
        }
    }
    os_puts("[usoak] scheduler churn OK");
    return 0;
}
