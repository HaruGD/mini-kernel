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
                            uint32_t base,
                            uint32_t accent) {
    window->surface = os_surface_create(display->width,
                                        display->height,
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
    OsWindowCreateRequest request;
    request.size = sizeof(request);
    request.abi_version = OS64_WINDOW_ABI_VERSION;
    request.command = OS_WINDOW_CREATE;
    request.flags = 0;
    request.request_id = request_id;
    request.content_generation = window->content_generation;
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

int window_demo_main(int argc, char** argv) {
    if (argc != 2) {
        os_puts("usage: usdk_c.elf window-present|window-hold|window-exit");
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
    if (os_streq(argv[1], "window-present-client") ||
        os_streq(argv[1], "window-hold-client") ||
        os_streq(argv[1], "window-exit-client")) {
        return run_client(argv[1]);
    }
    os_puts("[window-demo] unknown mode");
    return 1;
}
