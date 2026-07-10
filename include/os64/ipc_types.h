#ifndef OS64_IPC_TYPES_H
#define OS64_IPC_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_IPC_ABI_VERSION 1u
#define OS_IPC_MESSAGE_PAYLOAD_SIZE 64u
#define OS_IPC_SERVICE_NAME_MAX 16u

#define OS_IPC_MESSAGE_NONE 0u
#define OS_IPC_MESSAGE_REQUEST 1u
#define OS_IPC_MESSAGE_REPLY 2u
#define OS_IPC_MESSAGE_EVENT 3u

#define OS_IPC_FLAG_NONE 0u
#define OS_IPC_FLAG_REQUEST_REPLY (1u << 0)

typedef struct OsIpcMessage {
    uint32_t size;
    uint32_t sender_pid;
    uint32_t type;
    uint32_t flags;
    uint32_t length;
    uint32_t sender_generation;
    uint8_t payload[OS_IPC_MESSAGE_PAYLOAD_SIZE];
} OsIpcMessage;

#ifdef __cplusplus
#define OS64_IPC_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_IPC_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_IPC_STATIC_ASSERT(sizeof(OsIpcMessage) == 88, "OsIpcMessage ABI changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, size) == 0, "OsIpcMessage.size offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, sender_pid) == 4, "OsIpcMessage.sender_pid offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, type) == 8, "OsIpcMessage.type offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, flags) == 12, "OsIpcMessage.flags offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, length) == 16, "OsIpcMessage.length offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, sender_generation) == 20, "OsIpcMessage.sender_generation offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, payload) == 24, "OsIpcMessage.payload offset changed");

#undef OS64_IPC_STATIC_ASSERT

#endif
