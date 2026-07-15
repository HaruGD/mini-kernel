#include "window_compositor.h"

#include <os64/result.h>

long window_compositor_copy_full(uint32_t* destination,
                                 uint32_t destination_stride,
                                 const uint32_t* source,
                                 uint32_t source_stride,
                                 uint32_t width,
                                 uint32_t height) {
    if (destination == 0 || source == 0 || width == 0 || height == 0 ||
        destination_stride < width || source_stride < width) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t y = 0; y < height; y++) {
        uint32_t* destination_row = destination + y * destination_stride;
        const uint32_t* source_row = source + y * source_stride;
        for (uint32_t x = 0; x < width; x++) {
            destination_row[x] = source_row[x] & 0x00FFFFFFu;
        }
    }
    return OS_SUCCESS;
}

void window_compositor_clear(uint32_t* destination,
                             uint32_t destination_stride,
                             uint32_t width,
                             uint32_t height,
                             uint32_t color) {
    if (destination == 0 || destination_stride < width) {
        return;
    }
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            destination[y * destination_stride + x] = color & 0x00FFFFFFu;
        }
    }
}
