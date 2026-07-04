#include <os64/os64.h>

int main(void) {
    long result = os_service_register("demo", OS_SERVICE_FLAG_NONE);
    if (result < 0) {
        os_printf("[svc_demo] register failed %ld\n", result);
        return 1;
    }

    os_printf("[svc_demo] ready pid=%u\n", (uint32_t)os_getpid());
    while (1) {
        os_sleep(100000u);
    }
    return 0;
}
