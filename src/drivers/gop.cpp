#include "drivers/gop.h"

#include "kernel/boot_info.h"

GOPDriver gop;

GOPDriver::GOPDriver() {
    framebuffer = 0;
    info_state.framebuffer_addr = 0;
    info_state.framebuffer_size = 0;
    info_state.width = 0;
    info_state.height = 0;
    info_state.pixels_per_scanline = 0;
    info_state.format = GOP_PIXEL_FORMAT_RGB;
    ready_state = 0;
}

uint32_t GOPDriver::to_native_color(uint32_t color) const {
    uint32_t r = (color >> 16) & 0xFFU;
    uint32_t g = (color >> 8) & 0xFFU;
    uint32_t b = color & 0xFFU;
    if (info_state.format == GOP_PIXEL_FORMAT_BGR) {
        return (b << 16) | (g << 8) | r;
    }
    return (r << 16) | (g << 8) | b;
}

void GOPDriver::init_from_boot_info(const BootInfo* boot_info) {
    framebuffer = 0;
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
        boot_info->framebuffer_pixels_per_scanline == 0) {
        return;
    }

    framebuffer = (volatile uint32_t*)(uintptr_t)boot_info->framebuffer_addr;
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
    if (x >= info_state.width || y >= info_state.height) {
        return;
    }
    framebuffer[(uint64_t)y * info_state.pixels_per_scanline + x] = to_native_color(color);
}

void GOPDriver::fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!ready_state || framebuffer == 0 || width == 0 || height == 0) {
        return;
    }

    uint64_t x_end64 = (uint64_t)x + (uint64_t)width;
    uint64_t y_end64 = (uint64_t)y + (uint64_t)height;
    uint32_t x_end = x_end64 > info_state.width ? info_state.width : (uint32_t)x_end64;
    uint32_t y_end = y_end64 > info_state.height ? info_state.height : (uint32_t)y_end64;
    uint32_t native_color = to_native_color(color);

    for (uint32_t py = y; py < y_end; py++) {
        for (uint32_t px = x; px < x_end; px++) {
            framebuffer[(uint64_t)py * info_state.pixels_per_scanline + px] = native_color;
        }
    }
}

void GOPDriver::clear(uint32_t color) {
    if (!ready_state || framebuffer == 0) {
        return;
    }

    fill_rect(0, 0, info_state.width, info_state.height, color);
}
