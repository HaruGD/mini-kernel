#ifndef OS64_HANDLE_TYPES_H
#define OS64_HANDLE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define OS64_HANDLE_ABI_VERSION 1u
#define OS_HANDLE_TOKEN_SLOT_BITS 8u
#define OS_HANDLE_TOKEN_GENERATION_BITS 24u
#define OS_HANDLE_TOKEN_SLOT_MASK 0xFFu
#define OS_HANDLE_TOKEN_GENERATION_MAX 0x00FFFFFFu

typedef uint64_t OsHandle;

#define OS_HANDLE_TYPE_NONE 0u
#define OS_HANDLE_TYPE_VFS_FILE 1u
#define OS_HANDLE_TYPE_VFS_DIR 2u
#define OS_HANDLE_TYPE_SHARED_MEMORY 3u
#define OS_HANDLE_TYPE_GRAPHICS_SURFACE 4u

#define OS_HANDLE_RIGHT_READ 0x00000001u
#define OS_HANDLE_RIGHT_WRITE 0x00000002u
#define OS_HANDLE_RIGHT_SEEK 0x00000004u
#define OS_HANDLE_RIGHT_ENUMERATE 0x00000008u
#define OS_HANDLE_RIGHT_MAP 0x00000010u
#define OS_HANDLE_RIGHT_TRANSFER 0x00000020u

typedef struct OsSharedMemoryInfo {
    uint32_t owner_pid;
    uint32_t generation;
    uint32_t size;
    uint32_t page_count;
    uint32_t rights;
    uint32_t ref_count;
} OsSharedMemoryInfo;

typedef struct OsGraphicsSurfaceHandleInfo {
    uint32_t owner_pid;
    uint32_t generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t byte_size;
    uint32_t ref_count;
} OsGraphicsSurfaceHandleInfo;

#ifdef __cplusplus
#define OS64_HANDLE_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_HANDLE_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_HANDLE_STATIC_ASSERT(sizeof(OsHandle) == 8, "OsHandle ABI changed");
OS64_HANDLE_STATIC_ASSERT(sizeof(OsSharedMemoryInfo) == 24, "OsSharedMemoryInfo ABI changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsSharedMemoryInfo, owner_pid) == 0, "OsSharedMemoryInfo.owner_pid offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsSharedMemoryInfo, generation) == 4, "OsSharedMemoryInfo.generation offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsSharedMemoryInfo, size) == 8, "OsSharedMemoryInfo.size offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsSharedMemoryInfo, page_count) == 12, "OsSharedMemoryInfo.page_count offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsSharedMemoryInfo, rights) == 16, "OsSharedMemoryInfo.rights offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsSharedMemoryInfo, ref_count) == 20, "OsSharedMemoryInfo.ref_count offset changed");
OS64_HANDLE_STATIC_ASSERT(sizeof(OsGraphicsSurfaceHandleInfo) == 32, "OsGraphicsSurfaceHandleInfo ABI changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, owner_pid) == 0, "OsGraphicsSurfaceHandleInfo.owner_pid offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, generation) == 4, "OsGraphicsSurfaceHandleInfo.generation offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, width) == 8, "OsGraphicsSurfaceHandleInfo.width offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, height) == 12, "OsGraphicsSurfaceHandleInfo.height offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, stride_pixels) == 16, "OsGraphicsSurfaceHandleInfo.stride_pixels offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, pixel_format) == 20, "OsGraphicsSurfaceHandleInfo.pixel_format offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, byte_size) == 24, "OsGraphicsSurfaceHandleInfo.byte_size offset changed");
OS64_HANDLE_STATIC_ASSERT(offsetof(OsGraphicsSurfaceHandleInfo, ref_count) == 28, "OsGraphicsSurfaceHandleInfo.ref_count offset changed");

#undef OS64_HANDLE_STATIC_ASSERT

#endif
