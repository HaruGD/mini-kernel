#include <os64/os64.h>

static uint32_t command_from_text(const char* text) {
    if (text == 0 || os_streq(text, "ping")) {
        return OS_SERVICE_MANAGER_CMD_PING;
    }
    if (os_streq(text, "start")) {
        return OS_SERVICE_MANAGER_CMD_START;
    }
    if (os_streq(text, "stop")) {
        return OS_SERVICE_MANAGER_CMD_STOP;
    }
    if (os_streq(text, "restart")) {
        return OS_SERVICE_MANAGER_CMD_RESTART;
    }
    if (os_streq(text, "status")) {
        return OS_SERVICE_MANAGER_CMD_STATUS;
    }
    if (os_streq(text, "exit")) {
        return OS_SERVICE_MANAGER_CMD_EXIT;
    }
    return OS_SERVICE_MANAGER_CMD_NONE;
}

static const char* command_name(uint32_t command) {
    if (command == OS_SERVICE_MANAGER_CMD_START) {
        return "start";
    }
    if (command == OS_SERVICE_MANAGER_CMD_STOP) {
        return "stop";
    }
    if (command == OS_SERVICE_MANAGER_CMD_RESTART) {
        return "restart";
    }
    if (command == OS_SERVICE_MANAGER_CMD_STATUS) {
        return "status";
    }
    if (command == OS_SERVICE_MANAGER_CMD_EXIT) {
        return "exit";
    }
    return "ping";
}

static void copy_name(char* out, const char* name) {
    uint32_t i = 0;
    while (name != 0 && name[i] != '\0' && i + 1 < OS_SERVICE_NAME_MAX) {
        out[i] = name[i];
        i++;
    }
    out[i] = '\0';
}

static long ensure_service_manager(OsServiceInfo* info) {
    long result = os_service_find(OS_SERVICE_MANAGER_NAME, info);
    if (result == OS_SUCCESS) {
        return OS_SUCCESS;
    }
    if (os_run("serviced_c.elf") < 0) {
        return OS_ERR_NOT_READY;
    }
    return os_service_find(OS_SERVICE_MANAGER_NAME, info);
}

static int parse_reply(const OsIpcMessage* message,
                       const OsServiceInfo* manager,
                       const OsServiceManagerRequest* request,
                       OsServiceManagerReply* reply) {
    if (message == 0 || reply == 0 || message->type != OS_IPC_MESSAGE_REPLY ||
        message->length != sizeof(*reply)) {
        return 0;
    }
    if (manager == 0 || request == 0 || message->sender_pid != manager->owner_pid) {
        return 0;
    }
    os_memcpy(reply, message->payload, sizeof(*reply));
    return reply->size == sizeof(*reply) &&
           reply->command == request->command &&
           reply->request_id == request->request_id;
}

int main(int argc, char** argv) {
    uint32_t command = command_from_text(argc > 1 ? argv[1] : "ping");
    const char* target = argc > 2 ? argv[2] : "demo";
    if (command == OS_SERVICE_MANAGER_CMD_NONE) {
        os_puts("[usvcctl] usage: usvcctl_c.elf [ping|start|stop|restart|status|exit] [name]");
        return 1;
    }
    if (command == OS_SERVICE_MANAGER_CMD_PING || command == OS_SERVICE_MANAGER_CMD_EXIT) {
        target = OS_SERVICE_MANAGER_NAME;
    }

    OsServiceInfo manager;
    long result = ensure_service_manager(&manager);
    if (result < 0) {
        os_printf("[usvcctl] service manager unavailable %ld\n", result);
        return 1;
    }

    OsServiceManagerRequest request;
    request.size = sizeof(request);
    request.command = command;
    request.flags = 0;
    request.request_id = 1;
    copy_name(request.name, target);

    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_REQUEST);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    message.length = sizeof(request);
    os_memcpy(message.payload, &request, sizeof(request));

    result = os_msg_send(manager.owner_pid, &message);
    if (result < 0) {
        os_printf("[usvcctl] send failed %ld\n", result);
        return 1;
    }

    OsIpcMessage raw_reply;
    result = os_msg_wait(&raw_reply);
    if (result < 0) {
        os_printf("[usvcctl] wait failed %ld\n", result);
        return 1;
    }

    OsServiceManagerReply reply;
    if (!parse_reply(&raw_reply, &manager, &request, &reply)) {
        os_puts("[usvcctl] bad reply");
        return 1;
    }
    if (reply.result < 0) {
        os_printf("[usvcctl] %s %s failed %ld\n",
                  command_name(command),
                  reply.name,
                  (long)reply.result);
        return 1;
    }

    os_printf("[usvcctl] %s %s OK pid=%u state=%u gen=%u\n",
              command_name(command),
              reply.name,
              reply.pid,
              reply.state,
              reply.generation);
    os_reap_children();
    return 0;
}
