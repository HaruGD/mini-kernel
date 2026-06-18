#ifndef DRIVERS_GOP_H
#define DRIVERS_GOP_H

#include <stdint.h>

struct BootInfo;

#define GOP_PIXEL_FORMAT_RGB 0
#define GOP_PIXEL_FORMAT_BGR 1

struct GOPInfo {
    uint64_t framebuffer_addr;
    uint64_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
};

class GOPDriver {
    volatile uint32_t* framebuffer;
    GOPInfo info_state;
    uint8_t ready_state;

    uint32_t to_native_color(uint32_t color) const;

public:
    GOPDriver();

    void init_from_boot_info(const BootInfo* boot_info);
    int ready() const;
    const GOPInfo* info() const;

    void clear(uint32_t color);
    void putpixel(uint32_t x, uint32_t y, uint32_t color);
    void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
};

extern GOPDriver gop;

#endif
