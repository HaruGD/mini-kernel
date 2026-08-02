#ifndef OS64_IMAGE_INTERNAL_H
#define OS64_IMAGE_INTERNAL_H

#include "os64/image.h"
#include "os64/result.h"

long os_image_decode_png_internal(const uint8_t* bytes,
                                  uint32_t byte_count,
                                  OsImage* image);
long os_image_allocate_internal(uint32_t width,
                                uint32_t height,
                                OsImage* image);
uint32_t os_image_premultiply_internal(uint8_t red,
                                       uint8_t green,
                                       uint8_t blue,
                                       uint8_t alpha);

#endif
