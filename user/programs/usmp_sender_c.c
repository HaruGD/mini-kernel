#include <os64/os64.h>

int main(void) {
    OsIpcMessage message;
    os_msg_init(&message, OS_IPC_MESSAGE_EVENT);
    message.length = 1;
    message.payload[0] = 0x5A;
    long parent = os_getppid();
    if (parent <= 0 || os_msg_send((uint32_t)parent, &message) != OS_SUCCESS) {
        os_puts("[SMPW] sender FAIL\n");
        return 1;
    }
    os_puts("[SMPW] sender PASS\n");
    return 0;
}
