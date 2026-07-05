#include <os64/os64.h>

#define SERVICE_IDLE_TICKS 100000u

static void send_info_reply(uint32_t target_pid,
                            const OsServiceQueryRequest* request,
                            int result,
                            const OsGraphicsInfo* info) {
    OsDisplayServiceInfoReply reply;
    reply.size = sizeof(reply);
    reply.command = request != 0 ? request->command : OS_SERVICE_QUERY_DISPLAY_INFO;
    reply.result = result;
    reply.request_id = request != 0 ? request->request_id : 0;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = result == OS_SUCCESS ? 1u : 0u;
    reply.width = info != 0 ? info->width : 0;
    reply.height = info != 0 ? info->height : 0;
    reply.pixels_per_scanline = info != 0 ? info->pixels_per_scanline : 0;
    reply.format = info != 0 ? info->format : 0;
    reply.capabilities = OS_SERVICE_CAP_FRAMEBUFFER_INFO;
    reply.reserved = 0;

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REPLY);
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    os_msg_send(target_pid, &message);
}

static void handle_request(const OsIpcMessage* message) {
    OsServiceQueryRequest request;
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length != sizeof(request)) {
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    if (request.size != sizeof(request)) {
        return;
    }

    OsGraphicsInfo info;
    int result = OS_ERR_UNSUPPORTED;
    if (request.command == OS_SERVICE_QUERY_STATUS ||
        request.command == OS_SERVICE_QUERY_DISPLAY_INFO) {
        result = (int)os_gfx_get_info(&info);
    }
    send_info_reply(message->sender_pid, &request, result, result == OS_SUCCESS ? &info : 0);
}

int main(void) {
    long result = os_service_register("display", OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[displayd] register failed %ld\n", result);
        return 1;
    }

    os_printf("[displayd] ready pid=%u\n", (uint32_t)os_getpid());
    while (1) {
        os_sleep(SERVICE_IDLE_TICKS);
        OsIpcMessage message;
        result = os_msg_wait(&message);
        if (result < 0) {
            os_printf("[displayd] wait failed %ld\n", result);
            return 1;
        }
        handle_request(&message);
    }
}
