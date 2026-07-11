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

void os_msg_v2_init(OsIpcMessageV2* message, uint32_t type) {
    if (message == 0) {
        return;
    }
    message->size = sizeof(OsIpcMessageV2);
    message->abi_version = OS64_IPC_ABI_VERSION_V2;
    message->sender_pid = 0;
    message->sender_generation = 0;
    message->type = type;
    message->flags = OS_IPC_FLAG_NONE;
    message->length = 0;
    message->request_id = 0;
    message->reply_to = 0;
    message->handle_count = 0;
    for (uint32_t i = 0; i < OS_IPC_V2_MAX_HANDLES; i++) {
        message->handles[i] = 0;
    }
    os_memset(message->payload, 0, OS_IPC_V2_MESSAGE_PAYLOAD_SIZE);
}

void os_ipc_filter_init(OsIpcReceiveFilter* filter) {
    if (filter == 0) {
        return;
    }
    filter->size = sizeof(OsIpcReceiveFilter);
    filter->flags = OS_IPC_FLAG_NONE;
    filter->sender_pid = 0;
    filter->sender_generation = 0;
    filter->type = OS_IPC_MESSAGE_NONE;
    filter->reply_to = 0;
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

long os_ipc_features(uint32_t* abi_version_out, uint32_t* features_out) {
    return os_syscall2(OS_SYS_IPC_QUERY, (long)abi_version_out, (long)features_out);
}

uint32_t os_msg_next_request_id(void) {
    static uint32_t next_request_id = 1;
    uint32_t request_id = next_request_id++;
    if (next_request_id == 0) {
        next_request_id = 1;
    }
    return request_id == 0 ? os_msg_next_request_id() : request_id;
}

long os_msg_v2_send_to_identity(OsProcessIdentity target, const OsIpcMessageV2* message) {
    return os_syscall3(OS_SYS_IPC_V2_SEND_IDENTITY,
                       (long)target.pid,
                       (long)target.generation,
                       (long)message);
}

long os_msg_v2_recv(OsIpcMessageV2* message) {
    return os_syscall1(OS_SYS_IPC_V2_RECV, (long)message);
}

long os_msg_v2_recv_match(const OsIpcReceiveFilter* filter, OsIpcMessageV2* message) {
    return os_syscall2(OS_SYS_IPC_V2_RECV_MATCH, (long)filter, (long)message);
}

long os_msg_v2_request(OsProcessIdentity target,
                       OsIpcMessageV2* request,
                       OsIpcMessageV2* reply,
                       uint32_t timeout_ticks) {
    if (request == 0 || reply == 0 || target.pid == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    uint32_t request_id = request->request_id != 0 ? request->request_id : os_msg_next_request_id();
    request->request_id = request_id;
    request->reply_to = 0;
    request->flags |= OS_IPC_FLAG_REQUEST_REPLY;
    long result = os_msg_v2_send_to_identity(target, request);
    if (result < 0) {
        return result;
    }

    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE | OS_IPC_FILTER_REPLY_TO;
    filter.sender_pid = target.pid;
    filter.sender_generation = target.generation;
    filter.type = OS_IPC_MESSAGE_REPLY;
    filter.reply_to = request_id;

    uint32_t start = (uint32_t)os_time_ticks();
    while (1) {
        result = os_msg_v2_recv_match(&filter, reply);
        if (result == 0) {
            return 0;
        }
        if (result != OS_ERR_WOULD_BLOCK) {
            return result;
        }
        if (timeout_ticks != 0 && (uint32_t)(os_time_ticks() - start) >= timeout_ticks) {
            return OS_ERR_TIMEOUT;
        }
        os_sleep(1);
    }
}

OsProcessIdentity os_msg_sender_identity(const OsIpcMessage* message) {
    OsProcessIdentity identity;
    identity.pid = message != 0 ? message->sender_pid : 0;
    identity.generation = message != 0 ? message->sender_generation : 0;
    return identity;
}

OsProcessIdentity os_msg_v2_sender_identity(const OsIpcMessageV2* message) {
    OsProcessIdentity identity;
    identity.pid = message != 0 ? message->sender_pid : 0;
    identity.generation = message != 0 ? message->sender_generation : 0;
    return identity;
}
