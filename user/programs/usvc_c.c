#include <os64/os64.h>

int main(void) {
    long result = os_service_register("demo", OS_SERVICE_FLAG_NONE);
    if (result < 0) {
        os_printf("[usvc] register failed %ld\n", result);
        return 1;
    }

    result = os_service_register("demo", OS_SERVICE_FLAG_NONE);
    if (result != OS_ERR_ALREADY_EXISTS) {
        os_printf("[usvc] duplicate check failed %ld\n", result);
        return 1;
    }

    result = os_service_register("Demo", OS_SERVICE_FLAG_NONE);
    if (result != OS_ERR_INVALID_ARGUMENT) {
        os_printf("[usvc] invalid-name check failed %ld\n", result);
        return 1;
    }

    OsServiceInfo info;
    result = os_service_find("demo", &info);
    if (result < 0) {
        os_printf("[usvc] find failed %ld\n", result);
        return 1;
    }
    if (info.owner_pid != (uint32_t)os_getpid() ||
        info.state != OS_SERVICE_STATE_REGISTERED) {
        os_printf("[usvc] bad info pid=%u state=%u\n", info.owner_pid, info.state);
        return 1;
    }

    os_printf("[usvc] ready name=demo pid=%u gen=%u\n",
              info.owner_pid,
              info.generation);
    os_puts("[usvc] exiting without explicit unregister");
    return 0;
}
