#include "drivers/gop.h"

#include "kernel/boot_info.h"
#include "kernel/graphics/display_owner.h"

extern "C" {
    #include "heap.h"
}

GOPDriver gop;

GOPDriver::GOPDriver() {
    framebuffer = 0;
    gfx_surface_init(&surface, 0, 0, 0, 0, GOP_PIXEL_FORMAT_RGB, 0);
    gfx_surface_init(&back_buffer, 0, 0, 0, 0, GOP_PIXEL_FORMAT_RGB, 0);
    gfx_dirty_init(&dirty_tracker, 0);
    back_buffer_info.address = 0;
    back_buffer_info.size = 0;
    back_buffer_info.width = 0;
    back_buffer_info.height = 0;
    back_buffer_info.stride_pixels = 0;
    back_buffer_info.format = GOP_PIXEL_FORMAT_RGB;
    back_buffer_info.ready = 0;
    info_state.framebuffer_addr = 0;
    info_state.framebuffer_size = 0;
    info_state.width = 0;
    info_state.height = 0;
    info_state.pixels_per_scanline = 0;
    info_state.format = GOP_PIXEL_FORMAT_RGB;
    ready_state = 0;
    back_buffer_ready_state = 0;
}

void GOPDriver::init_from_boot_info(const BootInfo* boot_info) {
    framebuffer = 0;
    gfx_surface_init(&surface, 0, 0, 0, 0, GOP_PIXEL_FORMAT_RGB, 0);
    gfx_surface_init(&back_buffer, 0, 0, 0, 0, GOP_PIXEL_FORMAT_RGB, 0);
    gfx_dirty_init(&dirty_tracker, 0);
    back_buffer_info.address = 0;
    back_buffer_info.size = 0;
    back_buffer_info.width = 0;
    back_buffer_info.height = 0;
    back_buffer_info.stride_pixels = 0;
    back_buffer_info.format = GOP_PIXEL_FORMAT_RGB;
    back_buffer_info.ready = 0;
    info_state.framebuffer_addr = 0;
    info_state.framebuffer_size = 0;
    info_state.width = 0;
    info_state.height = 0;
    info_state.pixels_per_scanline = 0;
    info_state.format = GOP_PIXEL_FORMAT_RGB;
    ready_state = 0;
    back_buffer_ready_state = 0;

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

    OsRect bounds;
    bounds.x = 0;
    bounds.y = 0;
    bounds.width = (int32_t)boot_info->framebuffer_width;
    bounds.height = (int32_t)boot_info->framebuffer_height;
    gfx_dirty_init(&dirty_tracker, &bounds);

    info_state.framebuffer_addr = boot_info->framebuffer_addr;
    info_state.framebuffer_size = boot_info->framebuffer_size;
    info_state.width = boot_info->framebuffer_width;
    info_state.height = boot_info->framebuffer_height;
    info_state.pixels_per_scanline = boot_info->framebuffer_pixels_per_scanline;
    info_state.format = boot_info->framebuffer_format;
    ready_state = 1;
}

int GOPDriver::init_back_buffer() {
    if (!ready_state || framebuffer == 0) {
        return 0;
    }
    if (back_buffer_ready_state) {
        return 1;
    }

    uint64_t pixel_count = (uint64_t)info_state.width * info_state.height;
    if (info_state.width == 0 ||
        info_state.height == 0 ||
        pixel_count > UINT64_MAX / sizeof(uint32_t) ||
        pixel_count * sizeof(uint32_t) > SIZE_MAX) {
        return 0;
    }

    uint64_t size = pixel_count * sizeof(uint32_t);
    uint32_t* pixels = (uint32_t*)kmalloc((size_t)size);
    if (pixels == 0) {
        return 0;
    }

    if (!gfx_surface_init(&back_buffer,
                          pixels,
                          info_state.width,
                          info_state.height,
                          info_state.width,
                          info_state.format,
                          GFX_SURFACE_FLAG_OWNS_PIXELS)) {
        kfree(pixels);
        return 0;
    }

    back_buffer_info.address = (uint64_t)(uintptr_t)pixels;
    back_buffer_info.size = size;
    back_buffer_info.width = info_state.width;
    back_buffer_info.height = info_state.height;
    back_buffer_info.stride_pixels = info_state.width;
    back_buffer_info.format = info_state.format;
    back_buffer_info.ready = 1;
    back_buffer_ready_state = 1;
    gfx_dirty_mark_full(&dirty_tracker);
    return 1;
}

int GOPDriver::ready() const {
    return ready_state != 0;
}

int GOPDriver::back_buffer_ready() const {
    return back_buffer_ready_state != 0;
}

uint32_t GOPDriver::display_owner() const {
    return display_owner_current();
}

const GOPInfo* GOPDriver::info() const {
    return ready_state ? &info_state : 0;
}

const GOPBackBufferInfo* GOPDriver::back_buffer_info_state() const {
    return ready_state ? &back_buffer_info : 0;
}

GraphicsSurface* GOPDriver::back_buffer_surface() {
    return back_buffer_ready_state ? &back_buffer : 0;
}

const GraphicsDirtyTracker* GOPDriver::dirty_tracker_state() const {
    return ready_state ? &dirty_tracker : 0;
}

void GOPDriver::mark_dirty(const OsRect* rect) {
    gfx_dirty_mark(&dirty_tracker, rect);
}

void GOPDriver::mark_dirty_full() {
    gfx_dirty_mark_full(&dirty_tracker);
}

void GOPDriver::clear_dirty() {
    gfx_dirty_clear(&dirty_tracker);
}

int GOPDriver::present() {
    if (!ready_state || !back_buffer_ready_state || framebuffer == 0) {
        return 0;
    }
    DisplayOwnerToken token;
    display_owner_begin(DISPLAY_OWNER_GOP, &token);
    if (!token.acquired) {
        return 0;
    }
    gfx_present_dirty_surface(&surface, &back_buffer, &dirty_tracker);
    display_owner_end(&token);
    return 1;
}

void GOPDriver::putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!ready_state || framebuffer == 0) {
        return;
    }
    DisplayOwnerToken token;
    display_owner_begin(DISPLAY_OWNER_GOP, &token);
    if (!token.acquired) {
        return;
    }
    gfx_put_pixel(&surface, (int32_t)x, (int32_t)y, color);
    display_owner_end(&token);
}

void GOPDriver::fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!ready_state || framebuffer == 0 || width == 0 || height == 0) {
        return;
    }

    DisplayOwnerToken token;
    display_owner_begin(DISPLAY_OWNER_GOP, &token);
    if (!token.acquired) {
        return;
    }
    OsRect rect;
    rect.x = x > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)x;
    rect.y = y > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)y;
    rect.width = width > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)width;
    rect.height = height > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)height;
    gfx_fill_rect(&surface, &rect, color);
    display_owner_end(&token);
}

void GOPDriver::clear(uint32_t color) {
    if (!ready_state || framebuffer == 0) {
        return;
    }

    fill_rect(0, 0, info_state.width, info_state.height, color);
}
