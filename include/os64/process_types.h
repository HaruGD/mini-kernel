#ifndef OS64_PROCESS_TYPES_H
#define OS64_PROCESS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_PROCESS_ABI_VERSION 1u

#define OS_PROCESS_PERMISSION_SERVICE_DISCOVER (1u << 0)
#define OS_PROCESS_PERMISSION_SERVICE_REGISTER (1u << 1)
#define OS_PROCESS_PERMISSION_IPC              (1u << 2)
#define OS_PROCESS_PERMISSION_INPUT            (1u << 3)
#define OS_PROCESS_PERMISSION_DISPLAY          (1u << 4)
#define OS_PROCESS_PERMISSION_SHARED_SURFACE   (1u << 5)
#define OS_PROCESS_PERMISSION_MANAGE_CHILD     (1u << 6)
#define OS_PROCESS_PERMISSION_VALID_MASK       ((1u << 7) - 1u)
#define OS_PROCESS_PERMISSION_ALL              OS_PROCESS_PERMISSION_VALID_MASK

#define OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION \
    (OS_PROCESS_PERMISSION_SERVICE_DISCOVER | OS_PROCESS_PERMISSION_IPC | \
     OS_PROCESS_PERMISSION_SHARED_SURFACE)
#define OS_PROCESS_PERMISSION_PROFILE_GUI_SERVICE \
    (OS_PROCESS_PERMISSION_SERVICE_DISCOVER | OS_PROCESS_PERMISSION_SERVICE_REGISTER | \
     OS_PROCESS_PERMISSION_IPC | OS_PROCESS_PERMISSION_SHARED_SURFACE | \
     OS_PROCESS_PERMISSION_MANAGE_CHILD)

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
