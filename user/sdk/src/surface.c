#include "os64/surface.h"

#include <stdint.h>

#include "internal.h"

OsHandle os_surface_create(uint32_t width, uint32_t height, uint32_t pixel_format) {
    long result = os_syscall3(OS_SYS_SURFACE_CREATE, width, height, pixel_format);
    return result > 0 ? (OsHandle)result : 0;
}

long os_surface_get_info(OsHandle surface, OsGraphicsSurfaceHandleInfo* info) {
    return os_syscall2(OS_SYS_SURFACE_GET_INFO, (long)surface, (long)(uintptr_t)info);
}

void* os_surface_map(OsHandle surface, uint32_t map_flags) {
    long result = os_syscall2(OS_SYS_SURFACE_MAP, (long)surface, map_flags);
    return result > 0 ? (void*)(uintptr_t)result : 0;
}

long os_surface_unmap(OsHandle surface, void* address) {
    return os_syscall2(OS_SYS_SURFACE_UNMAP, (long)surface, (long)(uintptr_t)address);
}

long os_surface_close(OsHandle surface) {
    return os_syscall1(OS_SYS_SURFACE_CLOSE, (long)surface);
}
