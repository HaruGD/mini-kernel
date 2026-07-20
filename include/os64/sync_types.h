#ifndef OS64_SYNC_TYPES_H
#define OS64_SYNC_TYPES_H

#include <stdint.h>

#include "os64/handle_types.h"

#define OS64_SYNC_ABI_VERSION 1u
#define OS_SYNC_WAIT_FOREVER 0u
#define OS_SEMAPHORE_MAX_COUNT 0x7FFFFFFFu

typedef OsHandle OsMutex;
typedef OsHandle OsSemaphore;
typedef OsHandle OsCondition;

typedef struct OsOnce {
    OsMutex mutex;
    uint32_t initialized;
    uint32_t complete;
} OsOnce;

typedef long (*OsOnceInitializer)(void* context);

#endif
