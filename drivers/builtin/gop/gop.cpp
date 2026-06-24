#include "drivers/gop.h"

#include "kernel/boot_info.h"

GOPDriver gop;

GOPDriver::GOPDriver() {
    framebuffer = 0;
    gfx_surface_init(&surface, 0, 0, 0, 0, GOP_PIXEL_FORMAT_RGB, 0);
    info_state.framebuffer_addr = 0;
    info_state.framebuffer_size = 0;
    info_state.width = 0;
    info_state.height = 0;
    info_state.pixels_per_scanline = 0;
    info_state.format = GOP_PIXEL_FORMAT_RGB;
    ready_state = 0;
}

void GOPDriver::init_from_boot_info(const BootInfo* boot_info) {
    framebuffer = 0;
    gfx_surface_init(&surface, 0, 0, 0, 0, GOP_PIXEL_FORMAT_RGB, 0);
    info_state.framebuffer_addr = 0;
    info_state.framebuffer_size = 0;
    info_state.width = 0;
    info_state.height = 0;
    info_state.pixels_per_scanline = 0;
    info_state.format = GOP_PIXEL_FORMAT_RGB;
    ready_state = 0;

    if (boot_info == 0 ||
        boot_info->size < sizeof(BootInfo) ||
        !(boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER) ||
        boot_info->framebuffer_addr == 0 ||
        boot_info->framebuffer_width == 0 ||
        boot_info->framebuffer_height == 0 ||
        boot_info->framebuffer_pixels_per_scanline < boot_info->framebuffer_width ||
        (boot_info->framebuffer_format != GOP_PIXEL_FORMAT_RGB &&
         boot_info->framebuffer_format != GOP_PIXEL_FORMAT_BGR)) {
        return;
    }

    uint64_t required_pixels = (uint64_t)boot_info->framebuffer_pixels_per_scanline *
        boot_info->framebuffer_height;
    if (required_pixels > UINT64_MAX / sizeof(uint32_t) ||
        required_pixels * sizeof(uint32_t) > boot_info->framebuffer_size) {
        return;
    }

    framebuffer = (volatile uint32_t*)(uintptr_t)boot_info->framebuffer_addr;
    if (!gfx_surface_init(&surface,
                          (uint32_t*)(uintptr_t)boot_info->framebuffer_addr,
                          boot_info->framebuffer_width,
                          boot_info->framebuffer_height,
                          boot_info->framebuffer_pixels_per_scanline,
                          boot_info->framebuffer_format,
                          GFX_SURFACE_FLAG_FRAMEBUFFER)) {
        framebuffer = 0;
        return;
    }

    info_state.framebuffer_addr = boot_info->framebuffer_addr;
    info_state.framebuffer_size = boot_info->framebuffer_size;
    info_state.width = boot_info->framebuffer_width;
    info_state.height = boot_info->framebuffer_height;
    info_state.pixels_per_scanline = boot_info->framebuffer_pixels_per_scanline;
    info_state.format = boot_info->framebuffer_format;
    ready_state = 1;
}

int GOPDriver::ready() const {
    return ready_state != 0;
}

const GOPInfo* GOPDriver::info() const {
    return ready_state ? &info_state : 0;
}

void GOPDriver::putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!ready_state || framebuffer == 0) {
        return;
    }
    gfx_put_pixel(&surface, (int32_t)x, (int32_t)y, color);
}

void GOPDriver::fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!ready_state || framebuffer == 0 || width == 0 || height == 0) {
        return;
    }

    OsRect rect;
    rect.x = x > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)x;
    rect.y = y > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)y;
    rect.width = width > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)width;
    rect.height = height > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)height;
    gfx_fill_rect(&surface, &rect, color);
}

void GOPDriver::clear(uint32_t color) {
    if (!ready_state || framebuffer == 0) {
        return;
    }

    fill_rect(0, 0, info_state.width, info_state.height, color);
}
