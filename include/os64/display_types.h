#ifndef OS64_DISPLAY_TYPES_H
#define OS64_DISPLAY_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "os64/graphics_types.h"

#define OS64_DISPLAY_ABI_VERSION 1u

#define OS_DISPLAY_PRESENT_BEGIN  0x445001u
#define OS_DISPLAY_PRESENT_DAMAGE 0x445002u
#define OS_DISPLAY_PRESENT_COMMIT 0x445003u
#define OS_DISPLAY_PRESENT_REPLY  0x445004u

#define OS_DISPLAY_PRESENT_FLAG_FULL_FRAME 0x00000001u
#define OS_DISPLAY_PRESENT_VALID_FLAGS OS_DISPLAY_PRESENT_FLAG_FULL_FRAME

#define OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK 4u
#define OS_DISPLAY_DAMAGE_MAX_RECTS 64u
#define OS_DISPLAY_DAMAGE_MAX_CHUNKS \
    (OS_DISPLAY_DAMAGE_MAX_RECTS / OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK)

typedef struct OsDisplayPresentBegin {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t frame_generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t rect_count;
    uint32_t chunk_count;
} OsDisplayPresentBegin;

typedef struct OsDisplayPresentDamage {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t frame_generation;
    uint32_t chunk_index;
    uint32_t rect_count;
    OsRect rects[OS_DISPLAY_DAMAGE_RECTS_PER_CHUNK];
} OsDisplayPresentDamage;

typedef struct OsDisplayPresentCommit {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    uint32_t request_id;
    uint32_t frame_generation;
} OsDisplayPresentCommit;

typedef struct OsDisplayPresentReply {
    uint32_t size;
    uint32_t abi_version;
    uint32_t command;
    uint32_t flags;
    int32_t result;
    uint32_t request_id;
    uint32_t accepted_generation;
    uint32_t presented_rects;
} OsDisplayPresentReply;

#ifdef __cplusplus
#define OS64_DISPLAY_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_DISPLAY_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_DISPLAY_STATIC_ASSERT(sizeof(OsDisplayPresentBegin) == 48,
                           "OsDisplayPresentBegin ABI changed");
OS64_DISPLAY_STATIC_ASSERT(sizeof(OsDisplayPresentDamage) == 96,
                           "OsDisplayPresentDamage ABI changed");
OS64_DISPLAY_STATIC_ASSERT(sizeof(OsDisplayPresentCommit) == 24,
                           "OsDisplayPresentCommit ABI changed");
OS64_DISPLAY_STATIC_ASSERT(sizeof(OsDisplayPresentReply) == 32,
                           "OsDisplayPresentReply ABI changed");
OS64_DISPLAY_STATIC_ASSERT(offsetof(OsDisplayPresentDamage, rects) == 32,
                           "OsDisplayPresentDamage.rects offset changed");

#undef OS64_DISPLAY_STATIC_ASSERT

#endif
