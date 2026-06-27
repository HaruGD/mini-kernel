#ifndef DRIVERS_GOP_H
#define DRIVERS_GOP_H

#include <stdint.h>
#include "kernel/graphics/graphics2d.h"
#include "os64/graphics_types.h"

struct BootInfo;

#define GOP_PIXEL_FORMAT_RGB OS64_PIXEL_FORMAT_RGB
#define GOP_PIXEL_FORMAT_BGR OS64_PIXEL_FORMAT_BGR

struct GOPInfo {
    uint64_t framebuffer_addr;
    uint64_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
};

struct GOPBackBufferInfo {
    uint64_t address;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t format;
    uint32_t ready;
};

class GOPDriver {
    volatile uint32_t* framebuffer;
    GraphicsSurface surface;
    GraphicsSurface back_buffer;
    GraphicsDirtyTracker dirty_tracker;
    GOPBackBufferInfo back_buffer_info;
    GOPInfo info_state;
    uint8_t ready_state;
    uint8_t back_buffer_ready_state;

public:
    GOPDriver();

    void init_from_boot_info(const BootInfo* boot_info);
    int init_back_buffer();
    int ready() const;
    int back_buffer_ready() const;
    uint32_t display_owner() const;
    const GOPInfo* info() const;
    const GOPBackBufferInfo* back_buffer_info_state() const;
    GraphicsSurface* back_buffer_surface();
    const GraphicsDirtyTracker* dirty_tracker_state() const;
    void mark_dirty(const OsRect* rect);
    void mark_dirty_full();
    void clear_dirty();
    int present();

    void clear(uint32_t color);
    void putpixel(uint32_t x, uint32_t y, uint32_t color);
    void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
};

extern GOPDriver gop;

#endif
