#ifndef OS64_IPC_TYPES_H
#define OS64_IPC_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_IPC_ABI_VERSION 1u
#define OS64_IPC_ABI_VERSION_V1 1u
#define OS64_IPC_ABI_VERSION_V2 2u
#define OS_IPC_MESSAGE_PAYLOAD_SIZE 64u
#define OS_IPC_V2_MESSAGE_PAYLOAD_SIZE 96u
#define OS_IPC_V2_MAX_HANDLES 2u
#define OS_IPC_SERVICE_NAME_MAX 16u

#define OS_IPC_MESSAGE_NONE 0u
#define OS_IPC_MESSAGE_REQUEST 1u
#define OS_IPC_MESSAGE_REPLY 2u
#define OS_IPC_MESSAGE_EVENT 3u

#define OS_IPC_FLAG_NONE 0u
#define OS_IPC_FLAG_REQUEST_REPLY (1u << 0)
#define OS_IPC_FLAG_HAS_HANDLES (1u << 1)

#define OS_IPC_FEATURE_V2 (1u << 0)
#define OS_IPC_FEATURE_CORRELATION (1u << 1)
#define OS_IPC_FEATURE_HANDLE_TRANSFER (1u << 2)

#define OS_IPC_FILTER_SENDER (1u << 0)
#define OS_IPC_FILTER_TYPE (1u << 1)
#define OS_IPC_FILTER_REPLY_TO (1u << 2)

typedef struct OsIpcMessage {
    uint32_t size;
    uint32_t sender_pid;
    uint32_t type;
    uint32_t flags;
    uint32_t length;
    uint32_t sender_generation;
    uint8_t payload[OS_IPC_MESSAGE_PAYLOAD_SIZE];
} OsIpcMessage;

typedef struct OsIpcMessageV2 {
    uint32_t size;
    uint32_t abi_version;
    uint32_t sender_pid;
    uint32_t sender_generation;
    uint32_t type;
    uint32_t flags;
    uint32_t length;
    uint32_t request_id;
    uint32_t reply_to;
    uint32_t handle_count;
    uint64_t handles[OS_IPC_V2_MAX_HANDLES];
    uint8_t payload[OS_IPC_V2_MESSAGE_PAYLOAD_SIZE];
} OsIpcMessageV2;

typedef struct OsIpcReceiveFilter {
    uint32_t size;
    uint32_t flags;
    uint32_t sender_pid;
    uint32_t sender_generation;
    uint32_t type;
    uint32_t reply_to;
} OsIpcReceiveFilter;

#ifdef __cplusplus
#define OS64_IPC_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_IPC_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_IPC_STATIC_ASSERT(sizeof(OsIpcMessage) == 88, "OsIpcMessage ABI changed");
OS64_IPC_STATIC_ASSERT(sizeof(OsIpcMessageV2) == 152, "OsIpcMessageV2 ABI changed");
OS64_IPC_STATIC_ASSERT(sizeof(OsIpcReceiveFilter) == 24, "OsIpcReceiveFilter ABI changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, size) == 0, "OsIpcMessage.size offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, sender_pid) == 4, "OsIpcMessage.sender_pid offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, type) == 8, "OsIpcMessage.type offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, flags) == 12, "OsIpcMessage.flags offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, length) == 16, "OsIpcMessage.length offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, sender_generation) == 20, "OsIpcMessage.sender_generation offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessage, payload) == 24, "OsIpcMessage.payload offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, size) == 0, "OsIpcMessageV2.size offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, abi_version) == 4, "OsIpcMessageV2.abi_version offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, sender_pid) == 8, "OsIpcMessageV2.sender_pid offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, sender_generation) == 12, "OsIpcMessageV2.sender_generation offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, type) == 16, "OsIpcMessageV2.type offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, flags) == 20, "OsIpcMessageV2.flags offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, length) == 24, "OsIpcMessageV2.length offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, request_id) == 28, "OsIpcMessageV2.request_id offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, reply_to) == 32, "OsIpcMessageV2.reply_to offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, handle_count) == 36, "OsIpcMessageV2.handle_count offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, handles) == 40, "OsIpcMessageV2.handles offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcMessageV2, payload) == 56, "OsIpcMessageV2.payload offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcReceiveFilter, size) == 0, "OsIpcReceiveFilter.size offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcReceiveFilter, flags) == 4, "OsIpcReceiveFilter.flags offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcReceiveFilter, sender_pid) == 8, "OsIpcReceiveFilter.sender_pid offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcReceiveFilter, sender_generation) == 12, "OsIpcReceiveFilter.sender_generation offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcReceiveFilter, type) == 16, "OsIpcReceiveFilter.type offset changed");
OS64_IPC_STATIC_ASSERT(offsetof(OsIpcReceiveFilter, reply_to) == 20, "OsIpcReceiveFilter.reply_to offset changed");

#undef OS64_IPC_STATIC_ASSERT

#endif
