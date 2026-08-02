#ifndef OS64_IMAGE_H
#define OS64_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "os64/graphics_types.h"
#include "os64/surface.h"

#define OS64_IMAGE_ABI_VERSION 1u
#define OS_IMAGE_FORMAT_BGRA_PREMULTIPLIED 1u
#define OS_IMAGE_MAX_DIMENSION 4096u
#define OS_IMAGE_MAX_PIXELS (16u * 1024u * 1024u)
#define OS_IMAGE_MAX_BYTES (64u * 1024u * 1024u)

#define OS_IMAGE_DECODE_AUTO 0u
#define OS_IMAGE_DECODE_OSIMG 1u
#define OS_IMAGE_DECODE_BMP 2u
#define OS_IMAGE_DECODE_PNG 3u

#define OS_IMAGE_SCALE_NEAREST 0u

#define OS_IMAGE_OSIMG_MAGIC 0x4D49534Fu
#define OS_IMAGE_OSIMG_VERSION 1u

typedef struct OsImageFileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t payload_size;
    uint32_t checksum;
    uint32_t flags;
} OsImageFileHeader;

typedef struct OsImage {
    uint32_t size;
    uint32_t abi_version;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t* pixels;
    uint32_t allocation_bytes;
} OsImage;

void os_image_init(OsImage* image);
void os_image_destroy(OsImage* image);
long os_image_decode(const void* bytes,
                     uint32_t byte_count,
                     uint32_t format_hint,
                     OsImage* image);
long os_image_load(const char* path, OsImage* image);
long os_surface_canvas_draw_image(OsSurfaceCanvas* canvas,
                                  const OsImage* image,
                                  OsRect source,
                                  OsRect destination,
                                  uint32_t scale_mode,
                                  OsRect* damage_out);

#ifdef __cplusplus
#define OS64_IMAGE_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_IMAGE_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_IMAGE_STATIC_ASSERT(sizeof(OsImageFileHeader) == 36,
                         "OsImageFileHeader ABI changed");

#undef OS64_IMAGE_STATIC_ASSERT

#endif
