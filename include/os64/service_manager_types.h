#ifndef OS64_SERVICE_MANAGER_TYPES_H
#define OS64_SERVICE_MANAGER_TYPES_H

#include <stdint.h>
#include <stddef.h>

#include "os64/service_types.h"

#define OS64_SERVICE_MANAGER_ABI_VERSION 2u
#define OS_SERVICE_MANAGER_NAME "service"

#define OS_SERVICE_MANAGER_CMD_NONE 0u
#define OS_SERVICE_MANAGER_CMD_PING 1u
#define OS_SERVICE_MANAGER_CMD_START 2u
#define OS_SERVICE_MANAGER_CMD_STOP 3u
#define OS_SERVICE_MANAGER_CMD_RESTART 4u
#define OS_SERVICE_MANAGER_CMD_STATUS 5u
#define OS_SERVICE_MANAGER_CMD_EXIT 6u
#define OS_SERVICE_MANAGER_CMD_HEALTH 7u

#define OS_SERVICE_MANAGER_STATE_STOPPED 0u
#define OS_SERVICE_MANAGER_STATE_STARTING 1u
#define OS_SERVICE_MANAGER_STATE_RUNNING 2u
#define OS_SERVICE_MANAGER_STATE_STOPPING 3u
#define OS_SERVICE_MANAGER_STATE_FAILED 4u
#define OS_SERVICE_MANAGER_STATE_UNKNOWN 5u

#define OS_SERVICE_HEALTH_UNKNOWN 0u
#define OS_SERVICE_HEALTH_HEALTHY 1u
#define OS_SERVICE_HEALTH_UNRESPONSIVE 2u

#define OS_SERVICE_RESTART_DISABLED 0u
#define OS_SERVICE_RESTART_ON_FAILURE 1u

#define OS_SERVICE_FAILURE_NONE 0u
#define OS_SERVICE_FAILURE_LAUNCH 1u
#define OS_SERVICE_FAILURE_START_TIMEOUT 2u
#define OS_SERVICE_FAILURE_STOP_TIMEOUT 3u
#define OS_SERVICE_FAILURE_EXITED 4u
#define OS_SERVICE_FAILURE_HEALTH_TIMEOUT 5u
#define OS_SERVICE_FAILURE_DEPENDENCY 6u
#define OS_SERVICE_FAILURE_RESTART_LIMIT 7u
#define OS_SERVICE_FAILURE_PERMISSION 8u

typedef struct OsServiceManagerRequest {
    uint32_t size;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    char name[OS_SERVICE_NAME_MAX];
} OsServiceManagerRequest;

typedef struct OsServiceManagerReply {
    uint32_t size;
    uint32_t command;
    int32_t result;
    uint32_t pid;
    uint32_t state;
    uint32_t generation;
    uint32_t request_id;
    char name[OS_SERVICE_NAME_MAX];
    uint32_t last_failure;
    uint32_t restart_policy;
    uint32_t restart_count;
    uint32_t health;
    uint32_t permissions;
} OsServiceManagerReply;

#ifdef __cplusplus
#define OS64_SVCD_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_SVCD_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_SVCD_STATIC_ASSERT(sizeof(OsServiceManagerRequest) == 32,
                        "OsServiceManagerRequest ABI changed");
OS64_SVCD_STATIC_ASSERT(sizeof(OsServiceManagerReply) == 64,
                        "OsServiceManagerReply ABI changed");
OS64_SVCD_STATIC_ASSERT(offsetof(OsServiceManagerRequest, name) == 16,
                        "OsServiceManagerRequest.name offset changed");
OS64_SVCD_STATIC_ASSERT(offsetof(OsServiceManagerReply, name) == 28,
                        "OsServiceManagerReply.name offset changed");
OS64_SVCD_STATIC_ASSERT(offsetof(OsServiceManagerReply, permissions) == 60,
                        "OsServiceManagerReply.permissions offset changed");

#undef OS64_SVCD_STATIC_ASSERT

#endif
