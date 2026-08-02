#include <os64/os64.h>

#define SESSION_RECONNECT_TICKS 10u
#define SESSION_BACKGROUND_TOP OS_RGB(18, 28, 48)
#define SESSION_BACKGROUND_BOTTOM OS_RGB(8, 12, 22)

static OsWindow desktop;
static uint32_t desktop_ready;
static uint32_t session_generation = 1u;

static uint32_t blend_channel(uint32_t top, uint32_t bottom,
                              uint32_t y, uint32_t height) {
    if (height <= 1u) return top;
    return (top * (height - 1u - y) + bottom * y) / (height - 1u);
}

static void paint_background(void) {
    uint32_t height = desktop.surface_info.height;
    uint32_t width = desktop.surface_info.width;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t r = blend_channel((SESSION_BACKGROUND_TOP >> 16) & 0xFFu,
                                   (SESSION_BACKGROUND_BOTTOM >> 16) & 0xFFu,
                                   y, height);
        uint32_t g = blend_channel((SESSION_BACKGROUND_TOP >> 8) & 0xFFu,
                                   (SESSION_BACKGROUND_BOTTOM >> 8) & 0xFFu,
                                   y, height);
        uint32_t b = blend_channel(SESSION_BACKGROUND_TOP & 0xFFu,
                                   SESSION_BACKGROUND_BOTTOM & 0xFFu,
                                   y, height);
        uint32_t color = OS_RGB(r, g, b);
        uint32_t* row = desktop.pixels + y * desktop.surface_info.stride_pixels;
        for (uint32_t x = 0; x < width; x++) row[x] = color;
    }
}

static long create_desktop(void) {
    OsProcessIdentity owner;
    if (os_service_find_owner_identity("display", &owner) < 0 ||
        os_service_find_owner_identity("input", &owner) < 0 ||
        os_service_find_owner_identity("window", &owner) < 0) {
        return OS_ERR_NOT_READY;
    }
    long result = os_window_create_layer(&desktop, 0, 0, 0, 0,
                                         OS_WINDOW_FLAG_LAYER_DESKTOP);
    if (result < 0) return result;
    paint_background();
    result = os_window_damage_all(&desktop);
    if (result < 0) {
        os_window_destroy(&desktop);
        return result;
    }
    desktop_ready = 1;
    os_printf("[sessiond] desktop ready generation=%u window=%u:%u size=%ux%u layer=%u\n",
              session_generation, desktop.window_id,
              desktop.window_generation, desktop.surface_info.width,
              desktop.surface_info.height, desktop.layer);
    return OS_SUCCESS;
}

static void drop_desktop(void) {
    if (desktop.surface != 0) os_window_abandon(&desktop);
    desktop_ready = 0;
}

static void send_health(const OsIpcMessage* message,
                        const OsServiceQueryRequest* request) {
    OsServiceHealthReply reply;
    reply.size = sizeof(reply);
    reply.command = request->command;
    reply.result = desktop_ready ? OS_SUCCESS : OS_ERR_NOT_READY;
    reply.request_id = request->request_id;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = desktop_ready;
    reply.health = desktop_ready ? OS_SERVICE_HEALTH_HEALTHY
                                 : OS_SERVICE_HEALTH_UNRESPONSIVE;
    reply.reserved = session_generation;
    OsIpcMessage response;
    os_msg_init(&response, OS_IPC_MESSAGE_REPLY);
    response.length = sizeof(reply);
    os_memcpy(response.payload, &reply, sizeof(reply));
    os_msg_send_to_identity(os_msg_sender_identity(message), &response);
}

static void drain_service_requests(void) {
    OsIpcMessage message;
    while (os_msg_recv(&message) == OS_SUCCESS) {
        OsServiceQueryRequest request;
        if (message.type != OS_IPC_MESSAGE_REQUEST ||
            message.length != sizeof(request)) continue;
        os_memcpy(&request, message.payload, sizeof(request));
        if (request.size != sizeof(request) ||
            (request.command != OS_SERVICE_QUERY_HEALTH &&
             request.command != OS_SERVICE_QUERY_STATUS)) continue;
        send_health(&message, &request);
    }
}

int main(void) {
    OsThreadIdentity self;
    if (os_thread_self(&self) != OS_SUCCESS ||
        os_thread_set_affinity(self, 1u) != OS_SUCCESS) {
        os_puts("[sessiond] CPU0 ownership failed");
        return 1;
    }
    os_window_init(&desktop);
    if (os_service_register("session", OS_SERVICE_FLAG_SYSTEM) < 0) {
        os_puts("[sessiond] register failed");
        return 1;
    }
    os_printf("[sessiond] ready pid=%u generation=%u\n",
              (uint32_t)os_getpid(), session_generation);

    uint32_t next_reconnect = 0;
    while (1) {
        drain_service_requests();
        uint32_t now = (uint32_t)os_time_ticks();
        if (desktop_ready &&
            os_process_identity_alive(desktop.server) < 0) {
            os_puts("[sessiond] window service lost");
            drop_desktop();
            session_generation++;
            if (session_generation == 0) session_generation = 1;
            next_reconnect = now + SESSION_RECONNECT_TICKS;
        }
        if (!desktop_ready && (int32_t)(now - next_reconnect) >= 0) {
            long result = create_desktop();
            if (result < 0) next_reconnect = now + SESSION_RECONNECT_TICKS;
        }
        os_sleep(1);
    }
}
