#ifndef OS64_WINDOWD_COMPOSITOR_H
#define OS64_WINDOWD_COMPOSITOR_H

#include <stdint.h>

long window_compositor_copy_full(uint32_t* destination,
                                 uint32_t destination_stride,
                                 const uint32_t* source,
                                 uint32_t source_stride,
                                 uint32_t width,
                                 uint32_t height);
void window_compositor_clear(uint32_t* destination,
                             uint32_t destination_stride,
                             uint32_t width,
                             uint32_t height,
                             uint32_t color);

#endif
