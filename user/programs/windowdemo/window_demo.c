#include <os64/os64.h>

#define WINDOW_CLIENT_TIMEOUT_TICKS 200u

typedef struct DemoWindow {
    OsProcessIdentity server;
    OsHandle surface;
    uint32_t* pixels;
    OsGraphicsSurfaceHandleInfo surface_info;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t content_generation;
    int32_t x;
    int32_t y;
} DemoWindow;

static int query_display_info(OsDisplayServiceInfoReply* info) {
    OsProcessIdentity display;
    if (os_service_find_owner_identity("display", &display) < 0) {
        return 0;
    }
    OsServiceQueryRequest request;
    request.size = sizeof(request);
    request.command = OS_SERVICE_QUERY_DISPLAY_INFO;
    request.flags = 0;
    request.request_id = os_msg_next_request_id();
    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));
    if (os_msg_send_to_identity(display, &message) < 0 ||
        os_msg_wait_timeout(&message, WINDOW_CLIENT_TIMEOUT_TICKS) < 0 ||
        message.type != OS_IPC_MESSAGE_REPLY || message.length != sizeof(*info)) {
        return 0;
    }
    os_memcpy(info, message.payload, sizeof(*info));
    return info->size == sizeof(*info) && info->result == OS_SUCCESS &&
           info->request_id == request.request_id && info->ready != 0;
}

static void fill_pattern(uint32_t* pixels,
                         const OsGraphicsSurfaceHandleInfo* info,
                         uint32_t base,
                         uint32_t accent) {
    for (uint32_t y = 0; y < info->height; y++) {
        for (uint32_t x = 0; x < info->width; x++) {
            uint32_t color = base;
            if ((x >= info->width / 8u && x < info->width * 3u / 8u &&
                 y >= info->height / 6u && y < info->height * 5u / 6u) ||
                (x >= info->width * 5u / 8u && x < info->width * 7u / 8u &&
                 y >= info->height / 3u && y < info->height * 2u / 3u)) {
                color = accent;
            }
            pixels[y * info->stride_pixels + x] = color;
        }
    }
}

static void fill_damage(uint32_t* pixels,
                        const OsGraphicsSurfaceHandleInfo* info,
                        uint32_t color) {
    uint32_t x0 = info->width * 3u / 8u;
    uint32_t x1 = info->width * 5u / 8u;
    uint32_t y0 = info->height * 3u / 8u;
    uint32_t y1 = info->height * 5u / 8u;
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = x0; x < x1; x++) {
            pixels[y * info->stride_pixels + x] = color;
        }
    }
}

static void fill_rect(uint32_t* pixels,
                      const OsGraphicsSurfaceHandleInfo* info,
                      OsRect rect,
                      uint32_t color) {
    int32_t left = rect.x < 0 ? 0 : rect.x;
    int32_t top = rect.y < 0 ? 0 : rect.y;
    int64_t requested_right = (int64_t)rect.x + rect.width;
    int64_t requested_bottom = (int64_t)rect.y + rect.height;
    int32_t right = requested_right > info->width
        ? (int32_t)info->width : (int32_t)requested_right;
    int32_t bottom = requested_bottom > info->height
        ? (int32_t)info->height : (int32_t)requested_bottom;
    if (right <= left || bottom <= top) {
        return;
    }
    for (int32_t y = top; y < bottom; y++) {
        for (int32_t x = left; x < right; x++) {
            pixels[(uint32_t)y * info->stride_pixels + (uint32_t)x] = color;
        }
    }
}

static long wait_reply(DemoWindow* window,
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
    while ((uint32_t)(os_time_ticks() - start) < WINDOW_CLIENT_TIMEOUT_TICKS) {
        OsIpcMessageV2 message;
        long result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_SUCCESS) {
            if (message.length != sizeof(*reply)) {
                return OS_ERR_BAD_BUFFER;
            }
            os_memcpy(reply, message.payload, sizeof(*reply));
            if (reply->size != sizeof(*reply) ||
                reply->abi_version != OS64_WINDOW_ABI_VERSION ||
                reply->command != OS_WINDOW_REPLY ||
                reply->request_id != request_id || reply->operation != operation) {
                return OS_ERR_BAD_BUFFER;
            }
            return reply->result;
        }
        if (result != OS_ERR_WOULD_BLOCK) {
            return result;
        }
        os_sleep(1);
    }
    return OS_ERR_TIMEOUT;
}

static long send_payload(DemoWindow* window,
                         const void* payload,
                         uint32_t length,
                         uint32_t request_id,
                         uint32_t operation,
                         OsHandle surface,
                         OsWindowReply* reply) {
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

static int allocate_surface(DemoWindow* window,
                            const OsDisplayServiceInfoReply* display,
                            uint32_t width,
                            uint32_t height,
                            uint32_t base,
                            uint32_t accent) {
    window->surface = os_surface_create(width,
                                        height,
                                        display->format);
    window->pixels = window->surface != 0
        ? (uint32_t*)os_surface_map(window->surface,
                                   OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE)
        : 0;
    if (window->pixels == 0 ||
        os_surface_get_info(window->surface, &window->surface_info) < 0) {
        return 0;
    }
    fill_pattern(window->pixels, &window->surface_info, base, accent);
    return 1;
}

static void release_surface(DemoWindow* window) {
    if (window->pixels != 0) {
        os_surface_unmap(window->surface, window->pixels);
    }
    window->pixels = 0;
    if (window->surface != 0) {
        os_surface_close(window->surface);
    }
    window->surface = 0;
}

static long create_window(DemoWindow* window, OsWindowReply* reply) {
    uint32_t request_id = os_msg_next_request_id();
    OsWindowCreateGeometryRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_CREATE;
    request.flags = 0;
    request.request_id = request_id;
    request.content_generation = window->content_generation;
    request.x = window->x;
    request.y = window->y;
    request.width = window->surface_info.width;
    request.height = window->surface_info.height;
    request.stride_pixels = window->surface_info.stride_pixels;
    request.pixel_format = window->surface_info.pixel_format;
    long result = send_payload(window,
                               &request,
                               sizeof(request),
                               request_id,
                               OS_WINDOW_CREATE,
                               window->surface,
                               reply);
    if (result == OS_SUCCESS) {
        window->window_id = reply->window_id;
        window->window_generation = reply->window_generation;
    }
    return result;
}

static long replace_surface(DemoWindow* window,
                            OsHandle replacement,
                            const OsGraphicsSurfaceHandleInfo* info,
                            OsWindowReply* reply) {
    uint32_t request_id = os_msg_next_request_id();
    OsWindowSetSurfaceRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_SET_SURFACE;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.content_generation = ++window->content_generation;
    request.width = info->width;
    request.height = info->height;
    request.stride_pixels = info->stride_pixels;
    request.pixel_format = info->pixel_format;
    return send_payload(window,
                        &request,
                        sizeof(request),
                        request_id,
                        OS_WINDOW_SET_SURFACE,
                        replacement,
                        reply);
}

static long damage_window(DemoWindow* window, OsWindowReply* reply) {
    uint32_t request_id = os_msg_next_request_id();
    OsWindowDamageRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_DAMAGE;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.content_generation = ++window->content_generation;
    return send_payload(window,
                        &request,
                        sizeof(request),
                        request_id,
                        OS_WINDOW_DAMAGE,
                        0,
                        reply);
}

static long set_visibility(DemoWindow* window,
                           uint32_t operation,
                           OsWindowReply* reply) {
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
    return send_payload(window, &request, sizeof(request), request_id,
                        operation, 0, reply);
}

static long wait_window_event(DemoWindow* window,
                              uint32_t timeout_ticks,
                              OsWindowEvent* event) {
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE;
    filter.sender_pid = window->server.pid;
    filter.sender_generation = window->server.generation;
    filter.type = OS_IPC_MESSAGE_EVENT;
    uint32_t start = (uint32_t)os_time_ticks();
    while ((uint32_t)(os_time_ticks() - start) < timeout_ticks) {
        OsIpcMessageV2 message;
        long result = os_msg_v2_recv_match(&filter, &message);
        if (result == OS_SUCCESS) {
            if (message.length != sizeof(*event)) {
                return OS_ERR_BAD_BUFFER;
            }
            os_memcpy(event, message.payload, sizeof(*event));
            if (event->size != sizeof(*event) ||
                event->abi_version != OS64_WINDOW_ABI_VERSION ||
                event->window_id != window->window_id ||
                event->window_generation != window->window_generation ||
                event->event_sequence == 0) {
                return OS_ERR_BAD_BUFFER;
            }
            return OS_SUCCESS;
        }
        if (result != OS_ERR_WOULD_BLOCK) {
            return result;
        }
        os_sleep(1);
    }
    return OS_ERR_TIMEOUT;
}

static long drain_key_traffic(DemoWindow* window,
                              uint32_t* last_sequence,
                              uint32_t forbidden_keycode) {
    uint32_t quiet_ticks = 0;
    while (quiet_ticks < 20) {
        OsWindowEvent event;
        long result = wait_window_event(window, 2, &event);
        if (result == OS_ERR_TIMEOUT) {
            quiet_ticks += 2;
            continue;
        }
        if (result < 0 || event.event_sequence <= *last_sequence) {
            return result < 0 ? result : OS_ERR_BAD_BUFFER;
        }
        *last_sequence = event.event_sequence;
        quiet_ticks = 0;
        if (event.command == OS_WINDOW_EVENT_KEY &&
            event.input.data.key.keycode == forbidden_keycode) {
            return OS_ERR_PERMISSION_DENIED;
        }
    }
    return OS_SUCCESS;
}

static long move_window(DemoWindow* window,
                        int32_t x,
                        int32_t y,
                        OsWindowReply* reply) {
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
    long result = send_payload(window, &request, sizeof(request), request_id,
                               OS_WINDOW_MOVE, 0, reply);
    if (result == OS_SUCCESS) {
        window->x = x;
        window->y = y;
    }
    return result;
}

static long resize_window(DemoWindow* window,
                          OsHandle replacement,
                          const OsGraphicsSurfaceHandleInfo* info,
                          OsWindowReply* reply) {
    uint32_t request_id = os_msg_next_request_id();
    OsWindowResizeRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_RESIZE;
    request.flags = 0;
    request.request_id = request_id;
    request.window_id = window->window_id;
    request.window_generation = window->window_generation;
    request.content_generation = ++window->content_generation;
    request.width = info->width;
    request.height = info->height;
    request.stride_pixels = info->stride_pixels;
    request.pixel_format = info->pixel_format;
    return send_payload(window, &request, sizeof(request), request_id,
                        OS_WINDOW_RESIZE, replacement, reply);
}

static long send_damage_rects(DemoWindow* window,
                              const OsRect* rects,
                              uint32_t rect_count,
                              OsWindowReply* reply) {
    if (rects == 0 || rect_count == 0 ||
        rect_count > OS_WINDOW_DAMAGE_MAX_RECTS) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    uint32_t request_id = os_msg_next_request_id();
    uint32_t submission_id = os_msg_next_request_id();
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
    begin.content_generation = ++window->content_generation;
    begin.submission_id = submission_id;
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
        OsWindowDamageRectsRequest request;
        request.size = sizeof(request);
        request.abi_version = OS64_WINDOW_ABI_VERSION;
        request.command = OS_WINDOW_DAMAGE_RECTS;
        request.flags = 0;
        request.request_id = request_id;
        request.submission_id = submission_id;
        request.chunk_index = chunk;
        request.rect_count = rect_count - offset;
        if (request.rect_count > OS_WINDOW_DAMAGE_RECTS_PER_CHUNK) {
            request.rect_count = OS_WINDOW_DAMAGE_RECTS_PER_CHUNK;
        }
        for (uint32_t i = 0; i < OS_WINDOW_DAMAGE_RECTS_PER_CHUNK; i++) {
            if (i < request.rect_count) {
                request.rects[i] = rects[offset + i];
            } else {
                request.rects[i].x = 0;
                request.rects[i].y = 0;
                request.rects[i].width = 0;
                request.rects[i].height = 0;
            }
        }
        offset += request.rect_count;
        os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
        message.request_id = request_id;
        message.length = sizeof(request);
        os_memcpy(message.payload, &request, sizeof(request));
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
    commit.submission_id = submission_id;
    commit.reserved = 0;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.request_id = request_id;
    message.length = sizeof(commit);
    os_memcpy(message.payload, &commit, sizeof(commit));
    result = os_msg_v2_send_to_identity(window->server, &message);
    return result < 0 ? result
                      : wait_reply(window, request_id, OS_WINDOW_DAMAGE, reply);
}

static long destroy_window(DemoWindow* window, OsWindowReply* reply) {
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
    return send_payload(window,
                        &request,
                        sizeof(request),
                        request_id,
                        OS_WINDOW_DESTROY,
                        0,
                        reply);
}

static int run_client(const char* mode) {
    OsGraphicsInfo direct;
    long result = os_gfx_get_info(&direct);
    if (result != OS_ERR_PERMISSION_DENIED) {
        os_printf("[window-client] direct display unexpected %ld\n", result);
        return 1;
    }
    os_puts("[window-client] direct display denied");

    OsDisplayServiceInfoReply display;
    OsProcessIdentity initial_display;
    DemoWindow window;
    os_memset(&window, 0, sizeof(window));
    window.content_generation = 1;
    if (!query_display_info(&display) ||
        os_service_find_owner_identity("display", &initial_display) < 0 ||
        os_service_find_owner_identity("window", &window.server) < 0 ||
        !allocate_surface(&window,
                          &display,
                          display.width,
                          display.height,
                          OS_RGB(32, 48, 96),
                          OS_RGB(84, 152, 232))) {
        os_puts("[window-client] setup failed");
        release_surface(&window);
        return 1;
    }
    OsWindowReply reply;
    result = create_window(&window, &reply);
    if (result < 0) {
        os_printf("[window-client] CREATE failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_printf("[window-client] CREATE ACK id=%u generation=%u content=%u\n",
              window.window_id,
              window.window_generation,
              reply.accepted_content_generation);

    if (os_streq(mode, "window-exit-client")) {
        os_puts("[window-client] exiting without DESTROY");
        release_surface(&window);
        return 0;
    }
    if (os_streq(mode, "window-hold-client")) {
        os_puts("[window-client] holding for display restart");
        while (1) {
            OsProcessIdentity current_display;
            if (os_service_find_owner_identity("display", &current_display) ==
                    OS_SUCCESS &&
                (current_display.pid != initial_display.pid ||
                 current_display.generation != initial_display.generation)) {
                os_puts("[window-client] display restart observed");
                break;
            }
            os_sleep(5);
        }
    } else {
        DemoWindow replacement;
        os_memset(&replacement, 0, sizeof(replacement));
        if (!allocate_surface(&replacement,
                              &display,
                              display.width,
                              display.height,
                              OS_RGB(62, 80, 156),
                              OS_RGB(232, 188, 72))) {
            os_puts("[window-client] replacement allocation failed");
            release_surface(&window);
            return 1;
        }
        result = replace_surface(&window,
                                 replacement.surface,
                                 &replacement.surface_info,
                                 &reply);
        if (result < 0) {
            os_printf("[window-client] SET_SURFACE failed %ld\n", result);
            release_surface(&replacement);
            release_surface(&window);
            return 1;
        }
        release_surface(&window);
        window.surface = replacement.surface;
        window.pixels = replacement.pixels;
        window.surface_info = replacement.surface_info;
        replacement.surface = 0;
        replacement.pixels = 0;
        os_printf("[window-client] SET_SURFACE ACK content=%u\n",
                  reply.accepted_content_generation);

        fill_damage(window.pixels,
                    &window.surface_info,
                    OS_RGB(224, 112, 48));
        result = damage_window(&window, &reply);
        if (result < 0) {
            os_printf("[window-client] DAMAGE failed %ld\n", result);
            release_surface(&window);
            return 1;
        }
        os_printf("[window-client] DAMAGE ACK content=%u\n",
                  reply.accepted_content_generation);
        os_puts("[window-client] deterministic frame visible");
        os_sleep(100);
    }

    result = destroy_window(&window, &reply);
    if (result < 0) {
        os_printf("[window-client] DESTROY failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_puts("[window-client] DESTROY ACK");
    release_surface(&window);
    os_puts("[window-client] lifecycle OK");
    return 0;
}

static int setup_multi_window(DemoWindow* window,
                              uint32_t width,
                              uint32_t height,
                              int32_t x,
                              int32_t y,
                              uint32_t base,
                              uint32_t accent) {
    OsDisplayServiceInfoReply display;
    os_memset(window, 0, sizeof(*window));
    window->content_generation = 1;
    window->x = x;
    window->y = y;
    return query_display_info(&display) &&
           os_service_find_owner_identity("window", &window->server) == OS_SUCCESS &&
           allocate_surface(window, &display, width, height, base, accent);
}

static int run_multi_back_client(void) {
    DemoWindow window;
    if (!setup_multi_window(&window, 500, 360, 60, 100,
                            OS_RGB(36, 72, 164), OS_RGB(64, 188, 220))) {
        os_puts("[window-multi-back] setup failed");
        return 1;
    }
    OsWindowReply reply;
    long result = create_window(&window, &reply);
    if (result < 0) {
        os_printf("[window-multi-back] CREATE failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_printf("[window-multi-back] visible id=%u generation=%u\n",
              window.window_id, window.window_generation);
    os_sleep(1000);
    result = destroy_window(&window, &reply);
    release_surface(&window);
    if (result < 0) {
        os_printf("[window-multi-back] DESTROY failed %ld\n", result);
        return 1;
    }
    os_puts("[window-multi-back] lifecycle OK");
    return 0;
}

static int run_multi_front_client(void) {
    os_sleep(30);
    DemoWindow window;
    if (!setup_multi_window(&window, 360, 280, 260, 180,
                            OS_RGB(184, 64, 48), OS_RGB(244, 176, 64))) {
        os_puts("[window-multi-front] setup failed");
        return 1;
    }
    OsWindowReply reply;
    long result = create_window(&window, &reply);
    if (result < 0) {
        os_printf("[window-multi-front] CREATE failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_puts("[window-multi-front] overlap visible");
    os_sleep(150);

    result = set_visibility(&window, OS_WINDOW_HIDE, &reply);
    if (result < 0) {
        os_printf("[window-multi-front] HIDE failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_puts("[window-multi-front] hidden background revealed");
    os_sleep(150);

    result = set_visibility(&window, OS_WINDOW_SHOW, &reply);
    if (result < 0) {
        os_printf("[window-multi-front] SHOW failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_puts("[window-multi-front] shown and raised");
    os_sleep(150);

    result = move_window(&window, -80, 40, &reply);
    if (result < 0) {
        os_printf("[window-multi-front] MOVE failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_puts("[window-multi-front] moved with edge clipping");
    os_sleep(150);

    OsDisplayServiceInfoReply display;
    DemoWindow replacement;
    os_memset(&replacement, 0, sizeof(replacement));
    if (!query_display_info(&display) ||
        !allocate_surface(&replacement, &display, 300, 220,
                          OS_RGB(48, 152, 92), OS_RGB(136, 88, 216))) {
        os_puts("[window-multi-front] resize surface failed");
        release_surface(&window);
        return 1;
    }
    result = resize_window(&window, replacement.surface,
                           &replacement.surface_info, &reply);
    if (result < 0) {
        os_printf("[window-multi-front] RESIZE failed %ld\n", result);
        release_surface(&replacement);
        release_surface(&window);
        return 1;
    }
    release_surface(&window);
    window.surface = replacement.surface;
    window.pixels = replacement.pixels;
    window.surface_info = replacement.surface_info;
    replacement.surface = 0;
    replacement.pixels = 0;
    os_puts("[window-multi-front] resized atomically");
    os_sleep(150);

    OsRect rects[5] = {
        {-40, -30, 120, 100},
        {80, 30, 90, 70},
        {190, 130, 150, 120},
        {20, 160, 100, 80},
        {140, 80, 70, 60},
    };
    for (uint32_t i = 0; i < 5; i++) {
        fill_rect(window.pixels, &window.surface_info, rects[i],
                  OS_RGB(232, 72, 152));
    }
    result = send_damage_rects(&window, rects, 5, &reply);
    if (result < 0) {
        os_printf("[window-multi-front] DAMAGE chunks failed %ld\n", result);
        release_surface(&window);
        return 1;
    }
    os_puts("[window-multi-front] partial damage visible");
    os_sleep(150);

    result = destroy_window(&window, &reply);
    release_surface(&window);
    if (result < 0) {
        os_printf("[window-multi-front] DESTROY failed %ld\n", result);
        return 1;
    }
    os_puts("[window-multi-front] lifecycle OK");
    return 0;
}

static int run_input_a_client(void) {
    os_sleep(40);
    DemoWindow window;
    if (!setup_multi_window(&window, 280, 200, 80, 80,
                            OS_RGB(42, 92, 180), OS_RGB(96, 204, 232))) {
        os_puts("[window-input-a] setup failed");
        return 1;
    }
    OsWindowReply reply;
    if (create_window(&window, &reply) < 0 ||
        set_visibility(&window, OS_WINDOW_FOCUS, &reply) < 0) {
        os_puts("[window-input-a] create/focus failed");
        release_surface(&window);
        return 1;
    }
    OsWindowEvent event;
    uint32_t last_sequence = 0;
    int focused = 0;
    while (!focused && wait_window_event(&window, 5000, &event) == OS_SUCCESS) {
        if (event.event_sequence <= last_sequence) {
            os_printf("[window-input-a] sequence failure last=%u event=%u command=%u\n",
                      last_sequence, event.event_sequence, event.command);
            return 1;
        }
        last_sequence = event.event_sequence;
        focused = event.command == OS_WINDOW_EVENT_FOCUS_IN;
    }
    if (!focused) {
        os_puts("[window-input-a] focus-in missing");
        return 1;
    }
    os_puts("[window-input-a] focused ready");

    int got_f1 = 0;
    while (!got_f1 && wait_window_event(&window, 10000, &event) == OS_SUCCESS) {
        if (event.event_sequence <= last_sequence) {
            os_puts("[window-input-a] sequence failure");
            return 1;
        }
        last_sequence = event.event_sequence;
        got_f1 = event.command == OS_WINDOW_EVENT_KEY &&
                 event.input.type == OS_INPUT_EVENT_KEY &&
                 event.input.data.key.type == OS_KEY_EVENT_DOWN &&
                 event.input.data.key.keycode == OS_KEY_F1;
    }
    if (!got_f1) {
        os_puts("[window-input-a] key F1 missing");
        return 1;
    }
    os_puts("[window-input-a] key F1 received");

    if (drain_key_traffic(&window, &last_sequence, OS_KEY_F2) < 0) {
        os_puts("[window-input-a] pre-hide drain failed");
        return 1;
    }

    if (set_visibility(&window, OS_WINDOW_HIDE, &reply) < 0) {
        os_puts("[window-input-a] hide failed");
        return 1;
    }
    int focus_out = 0;
    while (!focus_out && wait_window_event(&window, 5000, &event) == OS_SUCCESS) {
        if (event.event_sequence <= last_sequence) {
            os_puts("[window-input-a] sequence failure");
            return 1;
        }
        last_sequence = event.event_sequence;
        focus_out = event.command == OS_WINDOW_EVENT_FOCUS_OUT;
        if (event.command == OS_WINDOW_EVENT_KEY &&
            event.input.data.key.keycode == OS_KEY_F2) {
            os_puts("[window-input-a] hidden key leak");
            return 1;
        }
    }
    if (!focus_out) {
        os_puts("[window-input-a] focus-out missing");
        return 1;
    }
    os_puts("[window-input-a] hidden focus-out");
    os_sleep(50);
    while (wait_window_event(&window, 1, &event) == OS_SUCCESS) {
        if (event.command == OS_WINDOW_EVENT_KEY &&
            event.input.data.key.keycode == OS_KEY_F2) {
            os_puts("[window-input-a] hidden key leak");
            return 1;
        }
    }
    if (destroy_window(&window, &reply) < 0) {
        os_puts("[window-input-a] destroy failed");
        return 1;
    }
    release_surface(&window);
    os_puts("[window-input-a] lifecycle OK");
    return 0;
}

static int run_input_b_client(void) {
    DemoWindow window;
    if (!setup_multi_window(&window, 300, 220, 360, 220,
                            OS_RGB(56, 148, 88), OS_RGB(224, 184, 72))) {
        os_puts("[window-input-b] setup failed");
        return 1;
    }
    OsWindowReply reply;
    if (create_window(&window, &reply) < 0) {
        os_puts("[window-input-b] create failed");
        release_surface(&window);
        return 1;
    }
    os_puts("[window-input-b] background ready");
    OsWindowEvent event;
    uint32_t last_sequence = 0;
    int focused = 0;
    while (!focused && wait_window_event(&window, 20000, &event) == OS_SUCCESS) {
        if (event.event_sequence <= last_sequence) {
            os_puts("[window-input-b] sequence failure");
            return 1;
        }
        last_sequence = event.event_sequence;
        if (event.command == OS_WINDOW_EVENT_KEY &&
            event.input.data.key.keycode == OS_KEY_F1) {
            os_puts("[window-input-b] background key leak");
            return 1;
        }
        focused = event.command == OS_WINDOW_EVENT_FOCUS_IN;
    }
    if (!focused) {
        os_puts("[window-input-b] fallback focus missing");
        return 1;
    }
    os_puts("[window-input-b] fallback focused");

    int got_f2 = 0;
    while (!got_f2 && wait_window_event(&window, 20000, &event) == OS_SUCCESS) {
        if (event.event_sequence <= last_sequence) {
            os_puts("[window-input-b] sequence failure");
            return 1;
        }
        last_sequence = event.event_sequence;
        got_f2 = event.command == OS_WINDOW_EVENT_KEY &&
                 event.input.type == OS_INPUT_EVENT_KEY &&
                 event.input.data.key.type == OS_KEY_EVENT_DOWN &&
                 event.input.data.key.keycode == OS_KEY_F2;
    }
    if (!got_f2) {
        os_puts("[window-input-b] key F2 missing");
        return 1;
    }
    os_puts("[window-input-b] key F2 received");
    if (drain_key_traffic(&window, &last_sequence, OS_KEY_F1) < 0) {
        os_puts("[window-input-b] pre-destroy drain failed");
        return 1;
    }
    if (destroy_window(&window, &reply) < 0) {
        os_puts("[window-input-b] destroy failed");
        return 1;
    }
    release_surface(&window);
    os_puts("[window-input-b] lifecycle OK");
    return 0;
}

static int launch_client(const char* mode) {
    char command[OS_PATH_MAX];
    const char* prefix = "usdk_c.elf ";
    uint32_t offset = 0;
    while (prefix[offset] != '\0') {
        command[offset] = prefix[offset];
        offset++;
    }
    uint32_t index = 0;
    while (mode[index] != '\0' && offset + 1 < sizeof(command)) {
        command[offset++] = mode[index++];
    }
    command[offset] = '\0';
    long pid = os_run_with_permissions(command,
                                       OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION);
    if (pid < 0 || os_set_background((uint32_t)pid, 1) < 0) {
        os_printf("[window-demo] launch failed %ld\n", pid);
        return 1;
    }
    os_printf("[window-demo] restricted %s launched pid=%u\n",
              mode,
              (uint32_t)pid);
    return 0;
}

static int launch_multi_clients(void) {
    if (launch_client("window-multi-back-client") != 0) {
        return 1;
    }
    if (launch_client("window-multi-front-client") != 0) {
        return 1;
    }
    os_puts("[window-demo] restricted multiwindow clients launched");
    return 0;
}

static int launch_input_clients(void) {
    if (launch_client("window-input-b-client") != 0 ||
        launch_client("window-input-a-client") != 0) {
        return 1;
    }
    os_puts("[window-demo] restricted input clients launched");
    return 0;
}

int window_demo_main(int argc, char** argv) {
    if (argc != 2) {
        os_puts("usage: usdk_c.elf window-present|window-hold|window-exit|window-multi|window-input");
        return 1;
    }
    if (os_streq(argv[1], "window-present")) {
        return launch_client("window-present-client");
    }
    if (os_streq(argv[1], "window-hold")) {
        return launch_client("window-hold-client");
    }
    if (os_streq(argv[1], "window-exit")) {
        return launch_client("window-exit-client");
    }
    if (os_streq(argv[1], "window-multi")) {
        return launch_multi_clients();
    }
    if (os_streq(argv[1], "window-multi-back-client")) {
        return run_multi_back_client();
    }
    if (os_streq(argv[1], "window-multi-front-client")) {
        return run_multi_front_client();
    }
    if (os_streq(argv[1], "window-input")) {
        return launch_input_clients();
    }
    if (os_streq(argv[1], "window-input-a-client")) {
        return run_input_a_client();
    }
    if (os_streq(argv[1], "window-input-b-client")) {
        return run_input_b_client();
    }
    if (os_streq(argv[1], "window-present-client") ||
        os_streq(argv[1], "window-hold-client") ||
        os_streq(argv[1], "window-exit-client")) {
        return run_client(argv[1]);
    }
    os_puts("[window-demo] unknown mode");
    return 1;
}
