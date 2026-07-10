#include <os64/os64.h>

static void send_status_reply(OsProcessIdentity target,
                              const OsServiceQueryRequest* request,
                              int result) {
    OsInputServiceStatusReply reply;
    reply.size = sizeof(reply);
    reply.command = request != 0 ? request->command : OS_SERVICE_QUERY_STATUS;
    reply.result = result;
    reply.request_id = request != 0 ? request->request_id : 0;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = result == OS_SUCCESS ? 1u : 0u;
    reply.capabilities = OS_SERVICE_CAP_KEYBOARD;
    reply.reserved = 0;

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REPLY);
    message.length = sizeof(reply);
    os_memcpy(message.payload, &reply, sizeof(reply));
    os_msg_send_to_identity(target, &message);
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

    int result = request.command == OS_SERVICE_QUERY_STATUS ? OS_SUCCESS : OS_ERR_UNSUPPORTED;
    send_status_reply(os_msg_sender_identity(message), &request, result);
}

int main(void) {
    long result = os_service_register("input", OS_SERVICE_FLAG_SYSTEM);
    if (result < 0) {
        os_printf("[inputd] register failed %ld\n", result);
        return 1;
    }

    os_printf("[inputd] ready pid=%u\n", (uint32_t)os_getpid());
    while (1) {
        OsIpcMessage message;
        result = os_msg_wait(&message);
        if (result < 0) {
            os_printf("[inputd] wait failed %ld\n", result);
            return 1;
        }
        handle_request(&message);
    }
}
