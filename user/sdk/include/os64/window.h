#ifndef OS64_WINDOW_H
#define OS64_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#include "os64/handle_types.h"
#include "os64/process_types.h"
#include "os64/window_types.h"

#define OS64_WINDOW_CLIENT_ABI_VERSION 1u
#define OS_WINDOW_DEFAULT_TIMEOUT_TICKS 200u

typedef struct OsWindowInfo {
    uint32_t size;
    uint32_t abi_version;
    uint32_t window_id;
    uint32_t window_generation;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t content_generation;
    uint32_t visible;
    uint32_t focused;
    uint32_t reserved;
} OsWindowInfo;

typedef struct OsWindow {
    uint32_t size;
    uint32_t abi_version;
    OsProcessIdentity server;
    OsHandle surface;
    uint32_t* pixels;
    OsGraphicsSurfaceHandleInfo surface_info;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t content_generation;
    int32_t x;
    int32_t y;
    uint32_t visible;
    uint32_t focused;
} OsWindow;

void os_window_init(OsWindow* window);
long os_window_create(OsWindow* window,
                      int32_t x,
                      int32_t y,
                      uint32_t width,
                      uint32_t height);
long os_window_destroy(OsWindow* window);
long os_window_attach_surface(OsWindow* window,
                              OsHandle surface,
                              uint32_t* pixels,
                              const OsGraphicsSurfaceHandleInfo* info);
long os_window_replace_surface(OsWindow* window);
long os_window_damage(OsWindow* window,
                      const OsRect* rects,
                      uint32_t rect_count);
long os_window_damage_all(OsWindow* window);
long os_window_move(OsWindow* window, int32_t x, int32_t y);
long os_window_resize(OsWindow* window, uint32_t width, uint32_t height);
long os_window_show(OsWindow* window);
long os_window_hide(OsWindow* window);
long os_window_focus(OsWindow* window);
long os_window_get_info(OsWindow* window, OsWindowInfo* info);
long os_window_poll_event(OsWindow* window, OsWindowEvent* event);
long os_window_wait_event(OsWindow* window,
                          OsWindowEvent* event,
                          uint32_t timeout_ticks);

#ifdef __cplusplus
#define OS64_WINDOW_CLIENT_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_WINDOW_CLIENT_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_WINDOW_CLIENT_STATIC_ASSERT(sizeof(OsWindowInfo) == 56,
                                 "OsWindowInfo ABI changed");
OS64_WINDOW_CLIENT_STATIC_ASSERT(offsetof(OsWindowInfo, x) == 16,
                                 "OsWindowInfo.x offset changed");
OS64_WINDOW_CLIENT_STATIC_ASSERT(offsetof(OsWindowInfo, focused) == 48,
                                 "OsWindowInfo.focused offset changed");

#undef OS64_WINDOW_CLIENT_STATIC_ASSERT

#endif
