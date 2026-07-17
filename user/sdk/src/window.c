#include "os64/os64.h"

static int identity_valid(OsProcessIdentity identity) {
    return identity.pid != 0 && identity.generation != 0;
}

static int window_valid(const OsWindow* window) {
    return window != 0 && window->size == sizeof(*window) &&
           window->abi_version == OS64_WINDOW_CLIENT_ABI_VERSION &&
           identity_valid(window->server) && window->surface != 0 &&
           window->pixels != 0 && window->window_id != 0 &&
           window->window_generation != 0;
}

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation == 0 ? 1u : generation;
}

static void release_surface(OsHandle surface, uint32_t* pixels) {
    if (surface != 0 && pixels != 0) {
        os_surface_unmap(surface, pixels);
    }
    if (surface != 0) {
        os_surface_close(surface);
    }
}

static long allocate_surface(uint32_t width,
                             uint32_t height,
                             uint32_t pixel_format,
                             OsHandle* surface,
                             uint32_t** pixels,
                             OsGraphicsSurfaceHandleInfo* info) {
    if (surface == 0 || pixels == 0 || info == 0 || width == 0 || height == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    *surface = os_surface_create(width, height, pixel_format);
    if (*surface == 0) {
        return OS_ERR_NO_RESOURCES;
    }
    *pixels = (uint32_t*)os_surface_map(
        *surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE);
    if (*pixels == 0) {
        os_surface_close(*surface);
        *surface = 0;
        return OS_ERR_NO_RESOURCES;
    }
    long result = os_surface_get_info(*surface, info);
    if (result < 0 || info->width != width || info->height != height ||
        info->stride_pixels < width || info->pixel_format != pixel_format) {
        release_surface(*surface, *pixels);
        *surface = 0;
        *pixels = 0;
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    return OS_SUCCESS;
}

static long wait_reply(const OsWindow* window,
                       uint32_t request_id,
                       uint32_t operation,
                       OsWindowReply* reply) {
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE |
                   OS_IPC_FILTER_REPLY_TO;
    filter.sender_pid = window->server.pid;
    filter.sender_generation = window->server.generation;
    filter.type = OS_IPC_MESSAGE_REPLY;
    filter.reply_to = request_id;
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) <
           OS_WINDOW_DEFAULT_TIMEOUT_TICKS) {
        OsIpcMessageV2 message;
        long result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_ERR_WOULD_BLOCK) {
            os_sleep(1);
            continue;
        }
        if (result < 0) {
            return result;
        }
        if (message.length != sizeof(*reply)) {
            return OS_ERR_BAD_BUFFER;
        }
        os_memcpy(reply, message.payload, sizeof(*reply));
        if (reply->size != sizeof(*reply) ||
            reply->abi_version != OS64_WINDOW_ABI_VERSION ||
            reply->command != OS_WINDOW_REPLY ||
            reply->request_id != request_id ||
            reply->operation != operation) {
            return OS_ERR_BAD_BUFFER;
        }
        return reply->result;
    }
    return OS_ERR_TIMEOUT;
}

static long query_info(OsWindow* window, OsWindowInfoReply* reply) {
    if (window == 0 || reply == 0 || !identity_valid(window->server)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t request_id = os_msg_next_request_id();
    OsWindowInfoRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_GET_INFO;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.reserved = 0;
    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.request_id = request_id;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));
    long result = os_msg_v2_send_to_identity(window->server, &message);
    if (result < 0) {
        return result;
    }
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE |
                   OS_IPC_FILTER_REPLY_TO;
    filter.sender_pid = window->server.pid;
    filter.sender_generation = window->server.generation;
    filter.type = OS_IPC_MESSAGE_REPLY;
    filter.reply_to = request_id;
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) <
           OS_WINDOW_DEFAULT_TIMEOUT_TICKS) {
        result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_ERR_WOULD_BLOCK) {
            os_sleep(1);
            continue;
        }
        if (result < 0) {
            return result;
        }
        if (message.length != sizeof(*reply)) {
            return OS_ERR_BAD_BUFFER;
        }
        os_memcpy(reply, message.payload, sizeof(*reply));
        if (reply->size != sizeof(*reply) ||
            reply->abi_version != OS64_WINDOW_ABI_VERSION ||
            reply->command != OS_WINDOW_GET_INFO ||
            reply->request_id != request_id) {
            return OS_ERR_BAD_BUFFER;
        }
        return reply->result;
    }
    return OS_ERR_TIMEOUT;
}

static long send_request(OsWindow* window,
                         const void* payload,
                         uint32_t length,
                         uint32_t request_id,
                         uint32_t operation,
                         OsHandle surface,
                         OsWindowReply* reply) {
    if (window == 0 || payload == 0 || reply == 0 ||
        !identity_valid(window->server) || length > OS_IPC_V2_MESSAGE_PAYLOAD_SIZE) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.request_id = request_id;
    message.length = length;
    if (surface != 0) {
        message.flags |= OS_IPC_FLAG_HAS_HANDLES;
        message.handle_count = 1;
        message.handles[0] = surface;
    }
    os_memcpy(message.payload, payload, length);
    long result = os_msg_v2_send_to_identity(window->server, &message);
    return result < 0 ? result
                      : wait_reply(window, request_id, operation, reply);
}

void os_window_init(OsWindow* window) {
    if (window == 0) {
        return;
    }
    os_memset(window, 0, sizeof(*window));
    window->size = sizeof(*window);
    window->abi_version = OS64_WINDOW_CLIENT_ABI_VERSION;
}

long os_window_create(OsWindow* window,
                      int32_t x,
                      int32_t y,
                      uint32_t width,
                      uint32_t height) {
    if (window == 0 || width == 0 || height == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    os_window_init(window);
    long result = os_service_find_owner_identity("window", &window->server);
    if (result < 0) {
        os_window_init(window);
        return result;
    }
    OsWindowInfoReply service_info;
    result = query_info(window, &service_info);
    if (result < 0 || width > service_info.width ||
        height > service_info.height ||
        (service_info.pixel_format != OS64_PIXEL_FORMAT_RGB &&
         service_info.pixel_format != OS64_PIXEL_FORMAT_BGR)) {
        os_window_init(window);
        return result < 0 ? result : OS_ERR_OUT_OF_RANGE;
    }
    uint32_t pixel_format = service_info.pixel_format;
    result = allocate_surface(width, height, pixel_format, &window->surface,
                              &window->pixels, &window->surface_info);
    if (result < 0) {
        os_window_init(window);
        return result;
    }
    uint32_t request_id = os_msg_next_request_id();
    OsWindowCreateGeometryRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_CREATE;
    request.flags = 0;
    request.request_id = request_id;
    request.content_generation = 1;
    request.x = x;
    request.y = y;
    request.width = window->surface_info.width;
    request.height = window->surface_info.height;
    request.stride_pixels = window->surface_info.stride_pixels;
    request.pixel_format = window->surface_info.pixel_format;
    OsWindowReply reply;
    result = send_request(window, &request, sizeof(request), request_id,
                          OS_WINDOW_CREATE, window->surface, &reply);
    if (result < 0 || reply.window_id == 0 || reply.window_generation == 0 ||
        reply.accepted_content_generation != 1) {
        release_surface(window->surface, window->pixels);
        os_window_init(window);
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    window->window_id = reply.window_id;
    window->window_generation = reply.window_generation;
    window->content_generation = reply.accepted_content_generation;
    window->x = x;
    window->y = y;
    window->visible = 1;
    return OS_SUCCESS;
}

long os_window_destroy(OsWindow* window) {
    if (!window_valid(window)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t request_id = os_msg_next_request_id();
    OsWindowDestroyRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_DESTROY;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.reserved = 0;
    OsWindowReply reply;
    long result = send_request(window, &request, sizeof(request), request_id,
                               OS_WINDOW_DESTROY, 0, &reply);
    if (result < 0) {
        return result;
    }
    release_surface(window->surface, window->pixels);
    os_window_init(window);
    return OS_SUCCESS;
}

long os_window_attach_surface(OsWindow* window,
                              OsHandle surface,
                              uint32_t* pixels,
                              const OsGraphicsSurfaceHandleInfo* info) {
    if (!window_valid(window) || surface == 0 || pixels == 0 || info == 0 ||
        info->width != window->surface_info.width ||
        info->height != window->surface_info.height ||
        info->stride_pixels < info->width ||
        info->pixel_format != window->surface_info.pixel_format) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t content_generation = next_generation(window->content_generation);
    uint32_t request_id = os_msg_next_request_id();
    OsWindowSetSurfaceRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_SET_SURFACE;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.content_generation = content_generation;
    request.width = info->width;
    request.height = info->height;
    request.stride_pixels = info->stride_pixels;
    request.pixel_format = info->pixel_format;
    OsWindowReply reply;
    long result = send_request(window, &request, sizeof(request), request_id,
                               OS_WINDOW_SET_SURFACE, surface, &reply);
    if (result < 0 || reply.accepted_content_generation != content_generation) {
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    release_surface(window->surface, window->pixels);
    window->surface = surface;
    window->pixels = pixels;
    window->surface_info = *info;
    window->content_generation = content_generation;
    return OS_SUCCESS;
}

long os_window_replace_surface(OsWindow* window) {
    if (!window_valid(window)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsHandle surface = 0;
    uint32_t* pixels = 0;
    OsGraphicsSurfaceHandleInfo info;
    long result = allocate_surface(window->surface_info.width,
                                   window->surface_info.height,
                                   window->surface_info.pixel_format,
                                   &surface, &pixels, &info);
    if (result < 0) {
        return result;
    }
    result = os_window_attach_surface(window, surface, pixels, &info);
    if (result < 0) {
        release_surface(surface, pixels);
    }
    return result;
}

long os_window_damage(OsWindow* window,
                      const OsRect* rects,
                      uint32_t rect_count) {
    if (!window_valid(window) || rects == 0 || rect_count == 0 ||
        rect_count > OS_WINDOW_DAMAGE_MAX_RECTS) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < rect_count; i++) {
        if (rects[i].width <= 0 || rects[i].height <= 0) {
            return OS_ERR_INVALID_ARGUMENT;
        }
    }
    uint32_t content_generation = next_generation(window->content_generation);
    uint32_t request_id = os_msg_next_request_id();
    uint32_t chunk_count =
        (rect_count + OS_WINDOW_DAMAGE_RECTS_PER_CHUNK - 1u) /
        OS_WINDOW_DAMAGE_RECTS_PER_CHUNK;
    OsWindowDamageBeginRequest begin;
    begin.size = sizeof(begin);
    begin.abi_version = OS64_WINDOW_ABI_VERSION;
    begin.command = OS_WINDOW_DAMAGE_BEGIN;
    begin.flags = 0;
    begin.request_id = request_id;
    begin.window_id = window->window_id;
    begin.window_generation = window->window_generation;
    begin.content_generation = content_generation;
    begin.submission_id = request_id;
    begin.rect_count = rect_count;
    begin.chunk_count = chunk_count;
    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.request_id = request_id;
    message.length = sizeof(begin);
    os_memcpy(message.payload, &begin, sizeof(begin));
    long result = os_msg_v2_send_to_identity(window->server, &message);
    if (result < 0) {
        return result;
    }
    uint32_t offset = 0;
    for (uint32_t chunk = 0; chunk < chunk_count; chunk++) {
        OsWindowDamageRectsRequest part;
        os_memset(&part, 0, sizeof(part));
        part.size = sizeof(part);
        part.abi_version = OS64_WINDOW_ABI_VERSION;
        part.command = OS_WINDOW_DAMAGE_RECTS;
        part.request_id = request_id;
        part.submission_id = request_id;
        part.chunk_index = chunk;
        part.rect_count = rect_count - offset;
        if (part.rect_count > OS_WINDOW_DAMAGE_RECTS_PER_CHUNK) {
            part.rect_count = OS_WINDOW_DAMAGE_RECTS_PER_CHUNK;
        }
        for (uint32_t i = 0; i < part.rect_count; i++) {
            part.rects[i] = rects[offset + i];
        }
        offset += part.rect_count;
        os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
        message.request_id = request_id;
        message.length = sizeof(part);
        os_memcpy(message.payload, &part, sizeof(part));
        result = os_msg_v2_send_to_identity(window->server, &message);
        if (result < 0) {
            return result;
        }
    }
    OsWindowDamageCommitRequest commit;
    commit.size = sizeof(commit);
    commit.abi_version = OS64_WINDOW_ABI_VERSION;
    commit.command = OS_WINDOW_DAMAGE_COMMIT;
    commit.flags = 0;
    commit.request_id = request_id;
    commit.submission_id = request_id;
    commit.reserved = 0;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.request_id = request_id;
    message.length = sizeof(commit);
    os_memcpy(message.payload, &commit, sizeof(commit));
    result = os_msg_v2_send_to_identity(window->server, &message);
    if (result < 0) {
        return result;
    }
    OsWindowReply reply;
    result = wait_reply(window, request_id, OS_WINDOW_DAMAGE, &reply);
    if (result < 0 || reply.accepted_content_generation != content_generation) {
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    window->content_generation = content_generation;
    return OS_SUCCESS;
}

long os_window_damage_all(OsWindow* window) {
    if (!window_valid(window)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsRect rect = {0, 0, (int32_t)window->surface_info.width,
                  (int32_t)window->surface_info.height};
    return os_window_damage(window, &rect, 1);
}

static long state_request(OsWindow* window, uint32_t operation) {
    if (!window_valid(window) ||
        (operation != OS_WINDOW_SHOW && operation != OS_WINDOW_HIDE &&
         operation != OS_WINDOW_FOCUS)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t request_id = os_msg_next_request_id();
    OsWindowStateRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = operation;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.reserved = 0;
    OsWindowReply reply;
    long result = send_request(window, &request, sizeof(request), request_id,
                               operation, 0, &reply);
    if (result == OS_SUCCESS) {
        if (operation == OS_WINDOW_SHOW) window->visible = 1;
        if (operation == OS_WINDOW_HIDE) {
            window->visible = 0;
            window->focused = 0;
        }
        if (operation == OS_WINDOW_FOCUS) window->focused = 1;
    }
    return result;
}

long os_window_show(OsWindow* window) { return state_request(window, OS_WINDOW_SHOW); }
long os_window_hide(OsWindow* window) { return state_request(window, OS_WINDOW_HIDE); }
long os_window_focus(OsWindow* window) { return state_request(window, OS_WINDOW_FOCUS); }

long os_window_move(OsWindow* window, int32_t x, int32_t y) {
    if (!window_valid(window)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t request_id = os_msg_next_request_id();
    OsWindowMoveRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_MOVE;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.x = x;
    request.y = y;
    request.reserved = 0;
    OsWindowReply reply;
    long result = send_request(window, &request, sizeof(request), request_id,
                               OS_WINDOW_MOVE, 0, &reply);
    if (result == OS_SUCCESS) {
        window->x = x;
        window->y = y;
    }
    return result;
}

long os_window_resize(OsWindow* window, uint32_t width, uint32_t height) {
    if (!window_valid(window) || width == 0 || height == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsHandle surface = 0;
    uint32_t* pixels = 0;
    OsGraphicsSurfaceHandleInfo info;
    long result = allocate_surface(width, height, window->surface_info.pixel_format,
                                   &surface, &pixels, &info);
    if (result < 0) {
        return result;
    }
    uint32_t content_generation = next_generation(window->content_generation);
    uint32_t request_id = os_msg_next_request_id();
    OsWindowResizeRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_RESIZE;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.content_generation = content_generation;
    request.width = info.width;
    request.height = info.height;
    request.stride_pixels = info.stride_pixels;
    request.pixel_format = info.pixel_format;
    OsWindowReply reply;
    result = send_request(window, &request, sizeof(request), request_id,
                          OS_WINDOW_RESIZE, surface, &reply);
    if (result < 0 || reply.accepted_content_generation != content_generation) {
        release_surface(surface, pixels);
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    release_surface(window->surface, window->pixels);
    window->surface = surface;
    window->pixels = pixels;
    window->surface_info = info;
    window->content_generation = content_generation;
    return OS_SUCCESS;
}

long os_window_get_info(OsWindow* window, OsWindowInfo* info) {
    if (!window_valid(window) || info == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsWindowInfoReply reply;
    long result = query_info(window, &reply);
    if (result < 0 || reply.window_id != window->window_id ||
        reply.window_generation != window->window_generation ||
        reply.width == 0 || reply.height == 0 ||
        reply.stride_pixels < reply.width ||
        reply.content_generation == 0) {
        return result < 0 ? result : OS_ERR_BAD_BUFFER;
    }
    window->x = reply.x;
    window->y = reply.y;
    window->surface_info.width = reply.width;
    window->surface_info.height = reply.height;
    window->surface_info.stride_pixels = reply.stride_pixels;
    window->surface_info.pixel_format = reply.pixel_format;
    window->content_generation = reply.content_generation;
    window->visible = reply.visible;
    window->focused = reply.focused;
    os_memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->abi_version = OS64_WINDOW_CLIENT_ABI_VERSION;
    info->window_id = window->window_id;
    info->window_generation = window->window_generation;
    info->x = window->x;
    info->y = window->y;
    info->width = window->surface_info.width;
    info->height = window->surface_info.height;
    info->stride_pixels = window->surface_info.stride_pixels;
    info->pixel_format = window->surface_info.pixel_format;
    info->content_generation = window->content_generation;
    info->visible = window->visible;
    info->focused = window->focused;
    return OS_SUCCESS;
}

static long receive_event(OsWindow* window, OsWindowEvent* event) {
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE;
    filter.sender_pid = window->server.pid;
    filter.sender_generation = window->server.generation;
    filter.type = OS_IPC_MESSAGE_EVENT;
    OsIpcMessageV2 message;
    long result = os_msg_v2_recv_match(&filter, &message);
    if (result < 0) {
        return result;
    }
    if (message.length != sizeof(*event)) {
        return OS_ERR_BAD_BUFFER;
    }
    os_memcpy(event, message.payload, sizeof(*event));
    if (event->size != sizeof(*event) ||
        event->abi_version != OS64_WINDOW_ABI_VERSION ||
        event->window_id != window->window_id ||
        event->window_generation != window->window_generation ||
        event->event_sequence == 0 ||
        (event->command != OS_WINDOW_EVENT_FOCUS_IN &&
         event->command != OS_WINDOW_EVENT_FOCUS_OUT &&
         event->command != OS_WINDOW_EVENT_KEY &&
         event->command != OS_WINDOW_EVENT_POINTER)) {
        return OS_ERR_BAD_BUFFER;
    }
    if (event->command == OS_WINDOW_EVENT_FOCUS_IN) window->focused = 1;
    if (event->command == OS_WINDOW_EVENT_FOCUS_OUT) window->focused = 0;
    return OS_SUCCESS;
}

long os_window_poll_event(OsWindow* window, OsWindowEvent* event) {
    if (!window_valid(window) || event == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return receive_event(window, event);
}

long os_window_wait_event(OsWindow* window,
                          OsWindowEvent* event,
                          uint32_t timeout_ticks) {
    if (!window_valid(window) || event == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t start = (uint32_t)os_time_ticks();
    while (1) {
        long result = receive_event(window, event);
        if (result != OS_ERR_WOULD_BLOCK) {
            return result;
        }
        if (timeout_ticks != 0 &&
            (uint32_t)(os_time_ticks() - start) >= timeout_ticks) {
            return OS_ERR_TIMEOUT;
        }
        os_sleep(1);
    }
}
