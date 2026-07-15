#include "window_protocol.h"

#include <os64/graphics_types.h>
#include <os64/result.h>

static int format_valid(uint32_t format) {
    return format == OS64_PIXEL_FORMAT_RGB || format == OS64_PIXEL_FORMAT_BGR;
}

static int surface_fields_valid(uint32_t width,
                                uint32_t height,
                                uint32_t stride_pixels,
                                uint32_t pixel_format) {
    return width != 0 && height != 0 && stride_pixels >= width &&
           format_valid(pixel_format);
}

long window_protocol_validate_create(const OsWindowCreateRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_CREATE || request->flags != 0 ||
        request->request_id == 0 || request->content_generation == 0 ||
        !surface_fields_valid(request->width,
                              request->height,
                              request->stride_pixels,
                              request->pixel_format)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

long window_protocol_validate_set_surface(const OsWindowSetSurfaceRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_SET_SURFACE || request->flags != 0 ||
        request->request_id == 0 || request->window_id == 0 ||
        request->window_generation == 0 || request->content_generation == 0 ||
        !surface_fields_valid(request->width,
                              request->height,
                              request->stride_pixels,
                              request->pixel_format)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

long window_protocol_validate_damage(const OsWindowDamageRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_DAMAGE || request->flags != 0 ||
        request->request_id == 0 || request->window_id == 0 ||
        request->window_generation == 0 || request->content_generation == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

long window_protocol_validate_destroy(const OsWindowDestroyRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_DESTROY || request->flags != 0 ||
        request->request_id == 0 || request->window_id == 0 ||
        request->window_generation == 0 || request->reserved != 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}
