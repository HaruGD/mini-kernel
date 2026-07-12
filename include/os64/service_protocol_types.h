#ifndef OS64_SERVICE_PROTOCOL_TYPES_H
#define OS64_SERVICE_PROTOCOL_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_SERVICE_PROTOCOL_ABI_VERSION 2u

#define OS_SERVICE_QUERY_STATUS 1u
#define OS_SERVICE_QUERY_DISPLAY_INFO 2u
#define OS_SERVICE_QUERY_HEALTH 3u

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

typedef struct OsServiceHealthReply {
    uint32_t size;
    uint32_t command;
    int32_t result;
    uint32_t request_id;
    uint32_t abi_version;
    uint32_t ready;
    uint32_t health;
    uint32_t reserved;
} OsServiceHealthReply;

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
OS64_SERVICE_PROTO_STATIC_ASSERT(sizeof(OsServiceHealthReply) == 32,
                                 "OsServiceHealthReply ABI changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceQueryRequest, command) == 4,
                                 "OsServiceQueryRequest.command offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceQueryRequest, size) == 0,
                                 "OsServiceQueryRequest.size offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceQueryRequest, flags) == 8,
                                 "OsServiceQueryRequest.flags offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceQueryRequest, request_id) == 12,
                                 "OsServiceQueryRequest.request_id offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, size) == 0,
                                 "OsInputServiceStatusReply.size offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, command) == 4,
                                 "OsInputServiceStatusReply.command offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, result) == 8,
                                 "OsInputServiceStatusReply.result offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, request_id) == 12,
                                 "OsInputServiceStatusReply.request_id offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, abi_version) == 16,
                                 "OsInputServiceStatusReply.abi_version offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, ready) == 20,
                                 "OsInputServiceStatusReply.ready offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, capabilities) == 24,
                                 "OsInputServiceStatusReply.capabilities offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsInputServiceStatusReply, reserved) == 28,
                                 "OsInputServiceStatusReply.reserved offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, size) == 0,
                                 "OsServiceHealthReply.size offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, command) == 4,
                                 "OsServiceHealthReply.command offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, result) == 8,
                                 "OsServiceHealthReply.result offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, request_id) == 12,
                                 "OsServiceHealthReply.request_id offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, abi_version) == 16,
                                 "OsServiceHealthReply.abi_version offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, ready) == 20,
                                 "OsServiceHealthReply.ready offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, health) == 24,
                                 "OsServiceHealthReply.health offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsServiceHealthReply, reserved) == 28,
                                 "OsServiceHealthReply.reserved offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, size) == 0,
                                 "OsDisplayServiceInfoReply.size offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, command) == 4,
                                 "OsDisplayServiceInfoReply.command offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, result) == 8,
                                 "OsDisplayServiceInfoReply.result offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, request_id) == 12,
                                 "OsDisplayServiceInfoReply.request_id offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, abi_version) == 16,
                                 "OsDisplayServiceInfoReply.abi_version offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, ready) == 20,
                                 "OsDisplayServiceInfoReply.ready offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, width) == 24,
                                 "OsDisplayServiceInfoReply.width offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, height) == 28,
                                 "OsDisplayServiceInfoReply.height offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, pixels_per_scanline) == 32,
                                 "OsDisplayServiceInfoReply.pixels_per_scanline offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, format) == 36,
                                 "OsDisplayServiceInfoReply.format offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, capabilities) == 40,
                                 "OsDisplayServiceInfoReply.capabilities offset changed");
OS64_SERVICE_PROTO_STATIC_ASSERT(offsetof(OsDisplayServiceInfoReply, reserved) == 44,
                                 "OsDisplayServiceInfoReply.reserved offset changed");

#undef OS64_SERVICE_PROTO_STATIC_ASSERT

#endif
