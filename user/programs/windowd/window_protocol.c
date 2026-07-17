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

long window_protocol_validate_create_geometry(
    const OsWindowCreateGeometryRequest* request) {
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

long window_protocol_validate_state(const OsWindowStateRequest* request,
                                    uint32_t command) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        (command != OS_WINDOW_SHOW && command != OS_WINDOW_HIDE &&
         command != OS_WINDOW_FOCUS) ||
        request->command != command || request->flags != 0 ||
        request->request_id == 0 || request->window_id == 0 ||
        request->window_generation == 0 || request->reserved != 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

long window_protocol_validate_move(const OsWindowMoveRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_MOVE || request->flags != 0 ||
        request->request_id == 0 || request->window_id == 0 ||
        request->window_generation == 0 || request->reserved != 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

long window_protocol_validate_resize(const OsWindowResizeRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_RESIZE || request->flags != 0 ||
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

long window_protocol_validate_damage_begin(
    const OsWindowDamageBeginRequest* request) {
    uint32_t expected_chunks = request != 0
        ? (request->rect_count + OS_WINDOW_DAMAGE_RECTS_PER_CHUNK - 1u) /
          OS_WINDOW_DAMAGE_RECTS_PER_CHUNK
        : 0;
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_DAMAGE_BEGIN || request->flags != 0 ||
        request->request_id == 0 || request->window_id == 0 ||
        request->window_generation == 0 || request->content_generation == 0 ||
        request->submission_id == 0 || request->rect_count == 0 ||
        request->rect_count > OS_WINDOW_DAMAGE_MAX_RECTS ||
        request->chunk_count == 0 ||
        request->chunk_count > OS_WINDOW_DAMAGE_MAX_CHUNKS ||
        request->chunk_count != expected_chunks) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}

long window_protocol_validate_damage_rects(
    const OsWindowDamageRectsRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_DAMAGE_RECTS || request->flags != 0 ||
        request->request_id == 0 || request->submission_id == 0 ||
        request->chunk_index >= OS_WINDOW_DAMAGE_MAX_CHUNKS ||
        request->rect_count == 0 ||
        request->rect_count > OS_WINDOW_DAMAGE_RECTS_PER_CHUNK) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < request->rect_count; i++) {
        if (request->rects[i].width <= 0 || request->rects[i].height <= 0) {
            return OS_ERR_INVALID_ARGUMENT;
        }
    }
    for (uint32_t i = request->rect_count;
         i < OS_WINDOW_DAMAGE_RECTS_PER_CHUNK; i++) {
        if (request->rects[i].x != 0 || request->rects[i].y != 0 ||
            request->rects[i].width != 0 || request->rects[i].height != 0) {
            return OS_ERR_INVALID_ARGUMENT;
        }
    }
    return OS_SUCCESS;
}

long window_protocol_validate_damage_commit(
    const OsWindowDamageCommitRequest* request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != OS64_WINDOW_ABI_VERSION ||
        request->command != OS_WINDOW_DAMAGE_COMMIT || request->flags != 0 ||
        request->request_id == 0 || request->submission_id == 0 ||
        request->reserved != 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return OS_SUCCESS;
}
