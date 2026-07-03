#include <os64/os64.h>

static uint32_t parse_u32_text(const char* text) {
    uint32_t value = 0;
    uint32_t i = 0;
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10u + (uint32_t)(text[i] - '0');
        i++;
    }
    return value;
}

static int payload_equals(const OsIpcMessage* message, const char* text) {
    uint32_t len = (uint32_t)os_strlen(text);
    if (message == 0 || message->length != len) {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (message->payload[i] != (uint8_t)text[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    os_puts("=== OS64 IPC ping sample ===");
    os_remove("/mem/upong.pid");

    if (os_run("upong_c.elf") < 0) {
        os_puts("[uping] failed to start upong");
        return 1;
    }

    char pid_text[16];
    if (os_read_text_file("/mem/upong.pid", pid_text, sizeof(pid_text)) < 0) {
        os_puts("[uping] failed to read upong pid");
        return 1;
    }
    uint32_t target_pid = parse_u32_text(pid_text);
    if (target_pid == 0) {
        os_puts("[uping] invalid upong pid");
        return 1;
    }

    OsIpcMessage request;
    os_msg_init(&request, OS_IPC_MESSAGE_REQUEST);
    request.flags = OS_IPC_FLAG_REQUEST_REPLY;
    const char* text = "ping";
    request.length = (uint32_t)os_strlen(text);
    os_memcpy(request.payload, text, request.length);

    long result = os_msg_send(target_pid, &request);
    if (result < 0) {
        os_printf("[uping] send failed %ld\n", result);
        return 1;
    }
    os_printf("[uping] sent request target=%u\n", target_pid);

    OsIpcMessage reply;
    result = os_msg_wait(&reply);
    if (result < 0) {
        os_printf("[uping] wait failed %ld\n", result);
        return 1;
    }
    if (reply.type != OS_IPC_MESSAGE_REPLY || !payload_equals(&reply, "pong")) {
        os_printf("[uping] bad reply type=%u len=%u\n", reply.type, reply.length);
        return 1;
    }

    os_printf("[uping] reply from pid=%u payload=pong\n", reply.sender_pid);
    os_puts("[uping] IPC roundtrip OK");
    os_reap_children();
    return 0;
}
