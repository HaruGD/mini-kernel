#ifndef OS64_GRAPHICS_TYPES_H
#define OS64_GRAPHICS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define OS64_GRAPHICS_ABI_VERSION 1u

#define OS64_PIXEL_FORMAT_RGB 0u
#define OS64_PIXEL_FORMAT_BGR 1u

#define OS64_COLOR_RGB(red, green, blue) \
    ((((uint32_t)(red) & 0xFFu) << 16) | (((uint32_t)(green) & 0xFFu) << 8) | \
     ((uint32_t)(blue) & 0xFFu))

#ifndef OS_GFX_PIXEL_RGB
#define OS_GFX_PIXEL_RGB OS64_PIXEL_FORMAT_RGB
#endif

#ifndef OS_GFX_PIXEL_BGR
#define OS_GFX_PIXEL_BGR OS64_PIXEL_FORMAT_BGR
#endif

#ifndef OS_RGB
#define OS_RGB(red, green, blue) OS64_COLOR_RGB((red), (green), (blue))
#endif

typedef struct OsPoint {
    int32_t x;
    int32_t y;
} OsPoint;

typedef struct OsRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} OsRect;

typedef struct OsSurfaceInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsSurfaceInfo;

typedef struct OsGraphicsInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
} OsGraphicsInfo;

#ifdef __cplusplus
#define OS64_GRAPHICS_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_GRAPHICS_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_GRAPHICS_STATIC_ASSERT(sizeof(OsPoint) == 8, "OsPoint ABI changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsPoint, x) == 0, "OsPoint.x offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsPoint, y) == 4, "OsPoint.y offset changed");

OS64_GRAPHICS_STATIC_ASSERT(sizeof(OsRect) == 16, "OsRect ABI changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsRect, x) == 0, "OsRect.x offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsRect, y) == 4, "OsRect.y offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsRect, width) == 8, "OsRect.width offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsRect, height) == 12, "OsRect.height offset changed");

OS64_GRAPHICS_STATIC_ASSERT(sizeof(OsSurfaceInfo) == 16, "OsSurfaceInfo ABI changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsSurfaceInfo, width) == 0, "OsSurfaceInfo.width offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsSurfaceInfo, height) == 4, "OsSurfaceInfo.height offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsSurfaceInfo, stride_pixels) == 8, "OsSurfaceInfo.stride_pixels offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsSurfaceInfo, pixel_format) == 12, "OsSurfaceInfo.pixel_format offset changed");

OS64_GRAPHICS_STATIC_ASSERT(sizeof(OsGraphicsInfo) == 16, "OsGraphicsInfo ABI changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsGraphicsInfo, width) == 0, "OsGraphicsInfo.width offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsGraphicsInfo, height) == 4, "OsGraphicsInfo.height offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsGraphicsInfo, pixels_per_scanline) == 8, "OsGraphicsInfo.pixels_per_scanline offset changed");
OS64_GRAPHICS_STATIC_ASSERT(offsetof(OsGraphicsInfo, format) == 12, "OsGraphicsInfo.format offset changed");

#undef OS64_GRAPHICS_STATIC_ASSERT

#endif
