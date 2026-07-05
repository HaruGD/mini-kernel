#include <os64/os64.h>

static int send_query(uint32_t target_pid, uint32_t command, uint32_t request_id) {
    OsServiceQueryRequest request;
    request.size = sizeof(request);
    request.command = command;
    request.flags = 0;
    request.request_id = request_id;

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));
    return (int)os_msg_send(target_pid, &message);
}

static int query_input(void) {
    OsServiceInfo info;
    long result = os_service_find("input", &info);
    if (result < 0) {
        os_printf("[usvcprobe] input find failed %ld\n", result);
        return 0;
    }
    if (send_query(info.owner_pid, OS_SERVICE_QUERY_STATUS, 11) < 0) {
        os_puts("[usvcprobe] input send failed");
        return 0;
    }

    OsIpcMessage message;
    result = os_msg_wait(&message);
    if (result < 0 || message.type != OS_IPC_MESSAGE_REPLY ||
        message.length != sizeof(OsInputServiceStatusReply)) {
        os_printf("[usvcprobe] input wait failed %ld\n", result);
        return 0;
    }

    OsInputServiceStatusReply reply;
    os_memcpy(&reply, message.payload, sizeof(reply));
    if (reply.size != sizeof(reply) ||
        reply.result != OS_SUCCESS ||
        reply.request_id != 11 ||
        reply.ready == 0 ||
        (reply.capabilities & OS_SERVICE_CAP_KEYBOARD) == 0) {
        os_puts("[usvcprobe] input bad reply");
        return 0;
    }

    os_printf("[usvcprobe] input OK pid=%u caps=0x%x\n", info.owner_pid, reply.capabilities);
    return 1;
}

static int query_display(void) {
    OsServiceInfo info;
    long result = os_service_find("display", &info);
    if (result < 0) {
        os_printf("[usvcprobe] display find failed %ld\n", result);
        return 0;
    }
    if (send_query(info.owner_pid, OS_SERVICE_QUERY_DISPLAY_INFO, 22) < 0) {
        os_puts("[usvcprobe] display send failed");
        return 0;
    }

    OsIpcMessage message;
    result = os_msg_wait(&message);
    if (result < 0 || message.type != OS_IPC_MESSAGE_REPLY ||
        message.length != sizeof(OsDisplayServiceInfoReply)) {
        os_printf("[usvcprobe] display wait failed %ld\n", result);
        return 0;
    }

    OsDisplayServiceInfoReply reply;
    os_memcpy(&reply, message.payload, sizeof(reply));
    if (reply.size != sizeof(reply) ||
        reply.result != OS_SUCCESS ||
        reply.request_id != 22 ||
        reply.ready == 0 ||
        reply.width == 0 ||
        reply.height == 0 ||
        (reply.capabilities & OS_SERVICE_CAP_FRAMEBUFFER_INFO) == 0) {
        os_puts("[usvcprobe] display bad reply");
        return 0;
    }

    os_printf("[usvcprobe] display OK pid=%u %ux%u stride=%u fmt=%u\n",
              info.owner_pid,
              reply.width,
              reply.height,
              reply.pixels_per_scanline,
              reply.format);
    return 1;
}

int main(void) {
    os_puts("=== OS64 service probe ===");
    if (!query_input()) {
        return 1;
    }
    if (!query_display()) {
        return 1;
    }
    os_puts("[usvcprobe] service probe OK");
    return 0;
}
