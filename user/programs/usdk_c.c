#include <os64/os64.h>

int window_demo_main(int argc, char** argv);

static int query_display(OsProcessIdentity* display,
                         OsDisplayServiceInfoReply* info) {
    long result = os_service_find_owner_identity("display", display);
    if (result < 0) {
        os_printf("[display-client] service lookup failed %ld\n", result);
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
    result = os_msg_send_to_identity(*display, &message);
    if (result < 0) {
        os_printf("[display-client] info send failed %ld\n", result);
        return 0;
    }
    result = os_msg_wait_timeout(&message, 50);
    if (result < 0 || message.type != OS_IPC_MESSAGE_REPLY ||
        message.length != sizeof(*info)) {
        os_printf("[display-client] info wait failed %ld\n", result);
        return 0;
    }
    os_memcpy(info, message.payload, sizeof(*info));
    if (info->size != sizeof(*info) || info->result != OS_SUCCESS ||
        info->request_id != request.request_id || info->ready == 0 ||
        info->width == 0 || info->height == 0 ||
        (info->format != OS64_PIXEL_FORMAT_RGB &&
         info->format != OS64_PIXEL_FORMAT_BGR)) {
        os_puts("[display-client] invalid display info");
        return 0;
    }
    return 1;
}

static void fill_surface(uint32_t* pixels,
                         const OsGraphicsSurfaceHandleInfo* info,
                         uint32_t color) {
    for (uint32_t y = 0; y < info->height; y++) {
        for (uint32_t x = 0; x < info->width; x++) {
            pixels[y * info->stride_pixels + x] = color;
        }
    }
}

static void fill_surface_rect(uint32_t* pixels,
                              const OsGraphicsSurfaceHandleInfo* info,
                              const OsRect* rect,
                              uint32_t color) {
    for (int32_t y = 0; y < rect->height; y++) {
        uint32_t row = (uint32_t)(rect->y + y) * info->stride_pixels;
        for (int32_t x = 0; x < rect->width; x++) {
            pixels[row + (uint32_t)(rect->x + x)] = color;
        }
    }
}

static int run_display_present_client(void) {
    OsGraphicsInfo direct_info;
    long result = os_gfx_get_info(&direct_info);
    if (result != OS_ERR_PERMISSION_DENIED) {
        os_printf("[display-client] direct display unexpected %ld\n", result);
        return 1;
    }
    os_puts("[display-client] direct display denied");

    OsProcessIdentity display;
    OsDisplayServiceInfoReply display_info;
    if (!query_display(&display, &display_info)) {
        return 1;
    }

    OsHandle surface = os_surface_create(display_info.width,
                                         display_info.height,
                                         display_info.format);
    if (surface == 0) {
        os_puts("[display-client] surface create failed");
        return 1;
    }
    OsGraphicsSurfaceHandleInfo surface_info;
    uint32_t* pixels = (uint32_t*)os_surface_map(
        surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE);
    if (pixels == 0 || os_surface_get_info(surface, &surface_info) < 0) {
        if (pixels != 0) {
            os_surface_unmap(surface, pixels);
        }
        os_surface_close(surface);
        os_puts("[display-client] surface map failed");
        return 1;
    }

    uint32_t generation = (uint32_t)os_time_ticks() + 1u;
    fill_surface(pixels,
                 &surface_info,
                 OS_RGB(38, 70, 126));
    OsDisplayPresentReply reply;
    result = os_display_present(display, surface, generation, 0, 0, 50, &reply);
    if (result < 0 || reply.accepted_generation != generation ||
        reply.presented_rects != 1) {
        os_printf("[display-client] full present failed %ld\n", result);
        os_surface_unmap(surface, pixels);
        os_surface_close(surface);
        return 1;
    }
    uint32_t full_generation = generation;

    OsRect damage;
    damage.width = (int32_t)(display_info.width / 4u);
    damage.height = (int32_t)(display_info.height / 4u);
    if (damage.width < 1) {
        damage.width = 1;
    }
    if (damage.height < 1) {
        damage.height = 1;
    }
    damage.x = (int32_t)display_info.width - damage.width;
    damage.y = (int32_t)display_info.height - damage.height;
    fill_surface_rect(pixels,
                      &surface_info,
                      &damage,
                      OS_RGB(46, 184, 92));
    generation++;
    result = os_display_present(display, surface, generation, &damage, 1, 50, &reply);
    if (result < 0 || reply.accepted_generation != generation ||
        reply.presented_rects != 1) {
        os_printf("[display-client] partial present failed %ld\n", result);
        os_surface_unmap(surface, pixels);
        os_surface_close(surface);
        return 1;
    }
    os_printf("[display-client] full ACK generation=%u\n", full_generation);
    os_printf("[display-client] partial ACK generation=%u\n", generation);

    os_surface_unmap(surface, pixels);
    os_surface_close(surface);
    os_puts("[display-client] present path OK");
    return 0;
}

int main(int argc, char** argv) {
    char cwd[OS_PATH_MAX];

    if (argc == 2 &&
        (os_streq(argv[1], "window-present") ||
         os_streq(argv[1], "window-hold") ||
         os_streq(argv[1], "window-exit") ||
         os_streq(argv[1], "window-present-client") ||
         os_streq(argv[1], "window-hold-client") ||
         os_streq(argv[1], "window-exit-client") ||
         os_streq(argv[1], "window-multi") ||
         os_streq(argv[1], "window-multi-back-client") ||
         os_streq(argv[1], "window-multi-front-client"))) {
        return window_demo_main(argc, argv);
    }

    if (argc == 2 && os_streq(argv[1], "display-present-client")) {
        return run_display_present_client();
    }
    if (argc == 2 && os_streq(argv[1], "display-present")) {
        long pid = os_run_with_permissions(
            "usdk_c.elf display-present-client",
            OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION);
        if (pid < 0) {
            os_printf("[display-test] client launch failed %ld\n", pid);
            return 1;
        }
        if (os_set_background((uint32_t)pid, 1) < 0) {
            os_puts("[display-test] client background failed");
            return 1;
        }
        os_puts("[display-test] restricted client launched");
        return 0;
    }

    if (argc == 2 && os_streq(argv[1], "surface-leak")) {
        OsHandle surface = os_surface_create(1025, 1, OS64_PIXEL_FORMAT_RGB);
        uint32_t* pixels = surface != 0
            ? (uint32_t*)os_surface_map(
                  surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE)
            : 0;
        if (pixels == 0) {
            os_puts("[usurface-leak] create/map failed");
            return 1;
        }
        pixels[0] = 0x00112233u;
        pixels[1024] = 0x00445566u;
        os_puts("[usurface-leak] mapped exit");
        return 0;
    }

    os_puts("=== OS64 User SDK v1 ===");
    os_printf("pid=%ld ppid=%ld argc=%d\n", os_getpid(), os_getppid(), argc);

    if (os_getcwd(cwd, sizeof(cwd)) == OS_OK) {
        os_printf("cwd=%s\n", cwd);
    }

    for (int i = 0; i < argc; i++) {
        os_printf("argv[%d]=%s\n", i, argv[i]);
    }

    os_puts("console, path, file, directory, and process APIs are ready.");
    return 0;
}
