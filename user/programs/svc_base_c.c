#include <os64/os64.h>

static void reply_health(const OsIpcMessage* message) {
    OsServiceQueryRequest request;
    if (message == 0 || message->type != OS_IPC_MESSAGE_REQUEST ||
        message->length != sizeof(request)) {
        return;
    }
    os_memcpy(&request, message->payload, sizeof(request));
    if (request.size != sizeof(request) || request.command != OS_SERVICE_QUERY_HEALTH) {
        return;
    }
    OsServiceHealthReply reply;
    reply.size = sizeof(reply);
    reply.command = request.command;
    reply.result = OS_SUCCESS;
    reply.request_id = request.request_id;
    reply.abi_version = OS64_SERVICE_PROTOCOL_ABI_VERSION;
    reply.ready = 1;
    reply.health = OS_SERVICE_HEALTH_HEALTHY;
    reply.reserved = 0;
    OsIpcMessage response;
    os_msg_init(&response, OS_IPC_MESSAGE_REPLY);
    response.length = sizeof(reply);
    os_memcpy(response.payload, &reply, sizeof(reply));
    os_msg_send_to_identity(os_msg_sender_identity(message), &response);
}

int main(void) {
    OsThreadIdentity self;
    if (os_thread_self(&self) != OS_SUCCESS ||
        os_thread_set_affinity(self, 1u) != OS_SUCCESS) {
        os_puts("[svc_base] CPU0 ownership failed");
        return 1;
    }
    long result = os_service_register("base", OS_SERVICE_FLAG_NONE);
    if (result < 0) {
        os_printf("[svc_base] register failed %ld\n", result);
        return 1;
    }
    os_printf("[svc_base] ready pid=%u\n", (uint32_t)os_getpid());
    while (1) {
        OsIpcMessage message;
        result = os_msg_wait(&message);
        if (result < 0) {
            return 1;
        }
        reply_health(&message);
    }
}
