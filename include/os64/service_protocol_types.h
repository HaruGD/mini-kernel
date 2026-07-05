#ifndef OS64_SERVICE_PROTOCOL_TYPES_H
#define OS64_SERVICE_PROTOCOL_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_SERVICE_PROTOCOL_ABI_VERSION 1u

#define OS_SERVICE_QUERY_STATUS 1u
#define OS_SERVICE_QUERY_DISPLAY_INFO 2u

#define OS_SERVICE_CAP_KEYBOARD (1u << 0)
#define OS_SERVICE_CAP_POINTER (1u << 1)
#define OS_SERVICE_CAP_FRAMEBUFFER_INFO (1u << 2)

typedef struct OsServiceQueryRequest {
    uint32_t size;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
} OsServiceQueryRequest;

typedef struct OsInputServiceStatusReply {
    uint32_t size;
    uint32_t command;
    int32_t result;
    uint32_t request_id;
    uint32_t abi_version;
    uint32_t ready;
    uint32_t capabilities;
    uint32_t reserved;
} OsInputServiceStatusReply;

typedef struct OsDisplayServiceInfoReply {
    uint32_t size;
    uint32_t command;
    int32_t result;
    uint32_t request_id;
    uint32_t abi_version;
    uint32_t ready;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
    uint32_t capabilities;
    uint32_t reserved;
} OsDisplayServiceInfoReply;

#ifdef __cplusplus
#define OS64_SERVICE_PROTO_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_SERVICE_PROTO_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_SERVICE_PROTO_STATIC_ASSERT(sizeof(OsServiceQueryRequest) == 16,
                                 "OsServiceQueryRequest ABI changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(sizeof(OsInputServiceStatusReply) == 32,
                                 "OsInputServiceStatusReply ABI changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(sizeof(OsDisplayServiceInfoReply) == 48,
                                 "OsDisplayServiceInfoReply ABI changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceQueryRequest, command) == 4,
                                 "OsServiceQueryRequest.command offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, capabilities) == 24,
                                 "OsInputServiceStatusReply.capabilities offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, width) == 24,
                                 "OsDisplayServiceInfoReply.width offset changed");

#undef OS64_SERVICE_PROTO_STATIC_ASSERT

#endif
