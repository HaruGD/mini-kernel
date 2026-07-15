#ifndef OS64_WINDOW_TYPES_H
#define OS64_WINDOW_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define OS64_WINDOW_ABI_VERSION 1u

#define OS_WINDOW_CREATE      0x574001u
#define OS_WINDOW_SET_SURFACE 0x574002u
#define OS_WINDOW_DAMAGE      0x574003u
#define OS_WINDOW_DESTROY     0x574004u
#define OS_WINDOW_REPLY       0x574005u

#define OS_WINDOW_FLAG_NONE 0u
#define OS_WINDOW_ID_FULLSCREEN 1u

typedef struct OsWindowCreateRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t content_generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsWindowCreateRequest;

typedef struct OsWindowSetSurfaceRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t content_generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsWindowSetSurfaceRequest;

typedef struct OsWindowDamageRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t content_generation;
} OsWindowDamageRequest;

typedef struct OsWindowDestroyRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t reserved;
} OsWindowDestroyRequest;

typedef struct OsWindowReply {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    int32_t result;
    uint32_t request_id;
    uint32_t operation;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t accepted_content_generation;
} OsWindowReply;

#ifdef __cplusplus
#define OS64_WINDOW_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_WINDOW_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowCreateRequest) == 40,
                          "OsWindowCreateRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowSetSurfaceRequest) == 48,
                          "OsWindowSetSurfaceRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowDamageRequest) == 32,
                          "OsWindowDamageRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowDestroyRequest) == 32,
                          "OsWindowDestroyRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowReply) == 40,
                          "OsWindowReply ABI changed");
OS64_WINDOW_STATIC_ASSERT(offsetof(OsWindowReply, result) == 16,
                          "OsWindowReply.result offset changed");

#undef OS64_WINDOW_STATIC_ASSERT

#endif
