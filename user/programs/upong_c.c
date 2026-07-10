#include <os64/os64.h>

static void u32_to_text(uint32_t value, char* buffer, uint32_t capacity) {
    char temp[16];
    uint32_t count = 0;

    if (capacity == 0) {
        return;
    }
    if (value == 0) {
        buffer[0] = '0';
        if (capacity > 1) {
            buffer[1] = '\0';
        }
        return;
    }

    while (value != 0 && count < sizeof(temp)) {
        temp[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    uint32_t out = 0;
    while (count > 0 && out + 1 < capacity) {
        buffer[out++] = temp[--count];
    }
    buffer[out] = '\0';
}

int main(void) {
    char pid_text[16];
    uint32_t pid = (uint32_t)os_getpid();
    u32_to_text(pid, pid_text, sizeof(pid_text));
    if (os_write_file("/mem/upong.pid", pid_text, (uint32_t)os_strlen(pid_text)) < 0) {
        os_puts("[upong] failed to publish pid");
        return 1;
    }

    os_printf("[upong] ready pid=%u\n", pid);
    os_yield();

    OsIpcMessage request;
    long result = os_msg_wait(&request);
    if (result < 0) {
        os_printf("[upong] wait failed %ld\n", result);
        return 1;
    }
    if (request.type != OS_IPC_MESSAGE_REQUEST) {
        os_printf("[upong] unexpected type=%u\n", request.type);
        return 1;
    }

    OsIpcMessage reply;
    os_msg_init(&reply, OS_IPC_MESSAGE_REPLY);
    const char* text = "pong";
    reply.length = (uint32_t)os_strlen(text);
    os_memcpy(reply.payload, text, reply.length);

    result = os_msg_send_to_identity(os_msg_sender_identity(&request), &reply);
    if (result < 0) {
        os_printf("[upong] reply failed %ld\n", result);
        return 1;
    }

    os_puts("[upong] reply sent");
    return 0;
}
