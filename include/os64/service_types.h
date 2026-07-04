#ifndef OS64_SERVICE_TYPES_H
#define OS64_SERVICE_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_SERVICE_ABI_VERSION 1u
#define OS_SERVICE_NAME_MAX 16u

#define OS_SERVICE_STATE_EMPTY 0u
#define OS_SERVICE_STATE_REGISTERED 1u

#define OS_SERVICE_FLAG_NONE 0u
#define OS_SERVICE_FLAG_SYSTEM (1u << 0)

typedef struct OsServiceInfo {
    uint32_t size;
    uint32_t owner_pid;
    uint32_t state;
    uint32_t flags;
    uint32_t generation;
    char name[OS_SERVICE_NAME_MAX];
} OsServiceInfo;

#ifdef __cplusplus
#define OS64_SERVICE_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_SERVICE_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_SERVICE_STATIC_ASSERT(sizeof(OsServiceInfo) == 36, "OsServiceInfo ABI changed");
OS64_SERVICE_STATIC_ASSERT(offsetof(OsServiceInfo, size) == 0, "OsServiceInfo.size offset changed");
OS64_SERVICE_STATIC_ASSERT(offsetof(OsServiceInfo, owner_pid) == 4, "OsServiceInfo.owner_pid offset changed");
OS64_SERVICE_STATIC_ASSERT(offsetof(OsServiceInfo, state) == 8, "OsServiceInfo.state offset changed");
OS64_SERVICE_STATIC_ASSERT(offsetof(OsServiceInfo, flags) == 12, "OsServiceInfo.flags offset changed");
OS64_SERVICE_STATIC_ASSERT(offsetof(OsServiceInfo, generation) == 16, "OsServiceInfo.generation offset changed");
OS64_SERVICE_STATIC_ASSERT(offsetof(OsServiceInfo, name) == 20, "OsServiceInfo.name offset changed");

#undef OS64_SERVICE_STATIC_ASSERT

#endif
