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

int main(int argc, char** argv) {
    const char* service_name = "demo";
    int crash_after_register = 0;
    if (argc > 1 && os_streq(argv[1], "restricted")) {
        OsGraphicsInfo graphics;
        OsServiceInfo info;
        long display_result = os_gfx_get_info(&graphics);
        long discover_result = os_service_find("service", &info);
        long child_result = os_run("uhello_c.elf");
        OsHandle surface_result = os_surface_create(4, 4, OS64_PIXEL_FORMAT_RGB);
        if (display_result != OS_ERR_PERMISSION_DENIED ||
            discover_result != OS_ERR_PERMISSION_DENIED ||
            child_result != OS_ERR_PERMISSION_DENIED ||
            surface_result != 0) {
            os_printf("[svc_perm] permission enforcement failed display=%ld discover=%ld child=%ld surface=%lu\n",
                      display_result,
                      discover_result,
                      child_result,
                      surface_result);
            return 1;
        }
        os_puts("[svc_perm] denied display, discovery, and child launch as expected");
        os_puts("[svc_perm] denied shared surface as expected");
        service_name = "restricted";
    } else if (argc > 1 && os_streq(argv[1], "crash")) {
        service_name = "crash";
        crash_after_register = 1;
    }

    long result = os_service_register(service_name, OS_SERVICE_FLAG_NONE);
    if (result < 0) {
        os_printf("[svc_demo] register failed %ld\n", result);
        return 1;
    }
    os_printf("[svc_demo] ready name=%s pid=%u\n", service_name, (uint32_t)os_getpid());
    if (crash_after_register) {
        os_puts("[svc_demo] intentional crash-loop exit");
        return 1;
    }
    while (1) {
        OsIpcMessage message;
        result = os_msg_wait(&message);
        if (result < 0) {
            return 1;
        }
        reply_health(&message);
    }
}
