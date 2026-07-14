#ifndef OS64_SDK_SURFACE_H
#define OS64_SDK_SURFACE_H

#include <stdint.h>

#include "os64/handle_types.h"
#include "os64/surface_types.h"

OsHandle os_surface_create(uint32_t width, uint32_t height, uint32_t pixel_format);
long os_surface_get_info(OsHandle surface, OsGraphicsSurfaceHandleInfo* info);
void* os_surface_map(OsHandle surface, uint32_t map_flags);
long os_surface_unmap(OsHandle surface, void* address);
long os_surface_close(OsHandle surface);

#endif
