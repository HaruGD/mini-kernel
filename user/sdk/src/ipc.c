#include <os64/os64.h>
#include "internal.h"

void os_msg_init(OsIpcMessage* message, uint32_t type) {
    if (message == 0) {
        return;
    }
    message->size = sizeof(OsIpcMessage);
    message->sender_pid = 0;
    message->type = type;
    message->flags = OS_IPC_FLAG_NONE;
    message->length = 0;
    message->sender_generation = 0;
    os_memset(message->payload, 0, OS_IPC_MESSAGE_PAYLOAD_SIZE);
}

long os_msg_send(uint32_t target_pid, const OsIpcMessage* message) {
    return os_syscall2(OS_SYS_IPC_SEND, (long)target_pid, (long)message);
}

long os_msg_send_to_identity(OsProcessIdentity target, const OsIpcMessage* message) {
    return os_syscall3(OS_SYS_IPC_SEND_IDENTITY,
                       (long)target.pid,
                       (long)target.generation,
                       (long)message);
}

long os_msg_recv(OsIpcMessage* message) {
    return os_syscall1(OS_SYS_IPC_RECV, (long)message);
}

long os_msg_wait(OsIpcMessage* message) {
    return os_msg_wait_timeout(message, 0);
}

long os_msg_wait_timeout(OsIpcMessage* message, uint32_t timeout_ticks) {
    return os_syscall2(OS_SYS_IPC_WAIT, (long)message, (long)timeout_ticks);
}

OsProcessIdentity os_msg_sender_identity(const OsIpcMessage* message) {
    OsProcessIdentity identity;
    identity.pid = message != 0 ? message->sender_pid : 0;
    identity.generation = message != 0 ? message->sender_generation : 0;
    return identity;
}
