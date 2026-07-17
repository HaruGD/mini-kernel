#include <os64/os64.h>

int main(void) {
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < 100u) {
        os_yield();
    }
    return 0;
}
