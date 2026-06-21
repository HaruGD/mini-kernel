#include <os64/os64.h>
#include "internal.h"

_Static_assert(sizeof(OsTimeInfo) == 24, "OsTimeInfo ABI changed");

uint64_t os_time_ticks(void) {
    return (uint64_t)os_syscall0(OS_SYS_TIME_TICKS);
}

uint32_t os_time_frequency(void) {
    return (uint32_t)os_syscall0(OS_SYS_TIME_FREQUENCY);
}

uint64_t os_time_milliseconds(void) {
    uint64_t ticks = os_time_ticks();
    uint32_t frequency = os_time_frequency();
    if (frequency == 0) {
        return 0;
    }
    return ((ticks / frequency) * 1000u) + (((ticks % frequency) * 1000u) / frequency);
}

void os_time_get(OsTimeInfo* info) {
    if (info == 0) {
        return;
    }
    info->ticks = os_time_ticks();
    info->frequency = os_time_frequency();
    info->milliseconds = info->frequency != 0
        ? ((info->ticks / info->frequency) * 1000u) +
          (((info->ticks % info->frequency) * 1000u) / info->frequency)
        : 0;
    info->reserved = 0;
}
