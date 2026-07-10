#ifndef OS64_PROCESS_TYPES_H
#define OS64_PROCESS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_PROCESS_ABI_VERSION 1u

typedef struct OsProcessIdentity {
    uint32_t pid;
    uint32_t generation;
} OsProcessIdentity;

#ifdef __cplusplus
#define OS64_PROCESS_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_PROCESS_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_PROCESS_STATIC_ASSERT(sizeof(OsProcessIdentity) == 8, "OsProcessIdentity ABI changed");
OS64_PROCESS_STATIC_ASSERT(offsetof(OsProcessIdentity, pid) == 0, "OsProcessIdentity.pid offset changed");
OS64_PROCESS_STATIC_ASSERT(offsetof(OsProcessIdentity, generation) == 4, "OsProcessIdentity.generation offset changed");

#undef OS64_PROCESS_STATIC_ASSERT

#endif
