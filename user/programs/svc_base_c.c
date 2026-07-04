#include <os64/os64.h>

int main(void) {
    long result = os_service_register("base", OS_SERVICE_FLAG_NONE);
    if (result < 0) {
        os_printf("[svc_base] register failed %ld\n", result);
        return 1;
    }

    os_printf("[svc_base] ready pid=%u\n", (uint32_t)os_getpid());
    while (1) {
        os_sleep(100000u);
    }
    return 0;
}
