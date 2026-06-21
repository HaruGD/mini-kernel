#ifndef OS64_TIME_H
#define OS64_TIME_H

#include <stdint.h>

typedef struct OsTimeInfo {
    uint64_t ticks;
    uint64_t milliseconds;
    uint32_t frequency;
    uint32_t reserved;
} OsTimeInfo;

uint64_t os_time_ticks(void);
uint32_t os_time_frequency(void);
uint64_t os_time_milliseconds(void);
void os_time_get(OsTimeInfo* info);

#endif
