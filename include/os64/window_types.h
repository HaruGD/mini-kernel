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
#define OS_WINDOW_SHOW          0x574006u
#define OS_WINDOW_HIDE          0x574007u
#define OS_WINDOW_MOVE          0x574008u
#define OS_WINDOW_RESIZE        0x574009u
#define OS_WINDOW_DAMAGE_BEGIN  0x57400Au
#define OS_WINDOW_DAMAGE_RECTS  0x57400Bu
#define OS_WINDOW_DAMAGE_COMMIT 0x57400Cu
#define OS_WINDOW_FOCUS         0x57400Du
#define OS_WINDOW_INPUT_EVENT   0x57400Eu
#define OS_WINDOW_GET_INFO      0x57400Fu

#define OS_WINDOW_EVENT_FOCUS_IN  0x574101u
#define OS_WINDOW_EVENT_FOCUS_OUT 0x574102u
#define OS_WINDOW_EVENT_KEY       0x574103u
#define OS_WINDOW_EVENT_POINTER   0x574104u
#define OS_WINDOW_EVENT_CLOSE_REQUEST 0x574105u
#define OS_WINDOW_EVENT_CONFIGURE     0x574106u
#define OS_WINDOW_EVENT_MINIMIZED     0x574107u
#define OS_WINDOW_EVENT_MAXIMIZED     0x574108u
#define OS_WINDOW_EVENT_RESTORED      0x574109u

#define OS_WINDOW_FLAG_NONE 0u
#define OS_WINDOW_FLAG_LAYER_DESKTOP        (1u << 0)
#define OS_WINDOW_FLAG_LAYER_PANEL          (1u << 1)
#define OS_WINDOW_FLAG_LAYER_SYSTEM_OVERLAY (1u << 2)
#define OS_WINDOW_FLAG_DECORATED            (1u << 8)
#define OS_WINDOW_FLAG_LAYER_MASK \
    (OS_WINDOW_FLAG_LAYER_DESKTOP | OS_WINDOW_FLAG_LAYER_PANEL | \
     OS_WINDOW_FLAG_LAYER_SYSTEM_OVERLAY)

#define OS_WINDOW_LAYER_DESKTOP        0u
#define OS_WINDOW_LAYER_NORMAL         1u
#define OS_WINDOW_LAYER_PANEL          2u
#define OS_WINDOW_LAYER_SYSTEM_OVERLAY 3u
#define OS_WINDOW_DECORATION_BORDER 4u
#define OS_WINDOW_DECORATION_TITLE_HEIGHT 24u
#define OS_WINDOW_DECORATION_BUTTON_SIZE 18u
#define OS_WINDOW_MIN_WIDTH 64u
#define OS_WINDOW_MIN_HEIGHT 48u
#define OS_WINDOW_ID_FULLSCREEN 1u
#define OS_WINDOW_MAX_WINDOWS 12u
#define OS_WINDOW_DAMAGE_MAX_RECTS 16u
#define OS_WINDOW_DAMAGE_RECTS_PER_CHUNK 4u
#define OS_WINDOW_DAMAGE_MAX_CHUNKS \
    (OS_WINDOW_DAMAGE_MAX_RECTS / OS_WINDOW_DAMAGE_RECTS_PER_CHUNK)

#include "os64/graphics_types.h"
#include "os64/input_types.h"

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

/* Extended CREATE layout. The original full-screen layout remains accepted. */
typedef struct OsWindowCreateGeometryRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t content_generation;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsWindowCreateGeometryRequest;

typedef struct OsWindowStateRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t reserved;
} OsWindowStateRequest;

typedef struct OsWindowMoveRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    int32_t x;
    int32_t y;
    uint32_t reserved;
} OsWindowMoveRequest;

typedef struct OsWindowResizeRequest {
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
} OsWindowResizeRequest;

typedef struct OsWindowDamageBeginRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t content_generation;
    uint32_t submission_id;
    uint32_t rect_count;
    uint32_t chunk_count;
} OsWindowDamageBeginRequest;

typedef struct OsWindowDamageRectsRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t submission_id;
    uint32_t chunk_index;
    uint32_t rect_count;
    OsRect rects[OS_WINDOW_DAMAGE_RECTS_PER_CHUNK];
} OsWindowDamageRectsRequest;

typedef struct OsWindowDamageCommitRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t submission_id;
    uint32_t reserved;
} OsWindowDamageCommitRequest;

typedef OsWindowStateRequest OsWindowFocusRequest;

typedef OsWindowStateRequest OsWindowInfoRequest;

typedef struct OsWindowInfoReply {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    int32_t result;
    uint32_t request_id;
    uint32_t window_id;
    uint32_t window_generation;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t content_generation;
    uint32_t visible;
    uint32_t focused;
    uint32_t capacity;
} OsWindowInfoReply;

typedef struct OsWindowInputForward {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t input_sequence;
    uint32_t reserved;
    OsInputEvent event;
} OsWindowInputForward;

typedef struct OsWindowEvent {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t event_sequence;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t reserved;
    OsInputEvent input;
} OsWindowEvent;

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
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowCreateGeometryRequest) == 48,
                          "OsWindowCreateGeometryRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowStateRequest) == 32,
                          "OsWindowStateRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowMoveRequest) == 40,
                          "OsWindowMoveRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowResizeRequest) == 48,
                          "OsWindowResizeRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowDamageBeginRequest) == 44,
                          "OsWindowDamageBeginRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowDamageRectsRequest) == 96,
                          "OsWindowDamageRectsRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowDamageCommitRequest) == 28,
                          "OsWindowDamageCommitRequest ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowInputForward) == 72,
                          "OsWindowInputForward ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowInfoReply) == 72,
                          "OsWindowInfoReply ABI changed");
OS64_WINDOW_STATIC_ASSERT(sizeof(OsWindowEvent) == 80,
                          "OsWindowEvent ABI changed");
OS64_WINDOW_STATIC_ASSERT(offsetof(OsWindowDamageRectsRequest, rects) == 32,
                          "OsWindowDamageRectsRequest.rects offset changed");
OS64_WINDOW_STATIC_ASSERT(offsetof(OsWindowInputForward, event) == 24,
                          "OsWindowInputForward.event offset changed");
OS64_WINDOW_STATIC_ASSERT(offsetof(OsWindowEvent, input) == 32,
                          "OsWindowEvent.input offset changed");
OS64_WINDOW_STATIC_ASSERT(offsetof(OsWindowInfoReply, result) == 16,
                          "OsWindowInfoReply.result offset changed");
OS64_WINDOW_STATIC_ASSERT(offsetof(OsWindowReply, result) == 16,
                          "OsWindowReply.result offset changed");

#undef OS64_WINDOW_STATIC_ASSERT

#endif
