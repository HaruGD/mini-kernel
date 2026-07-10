#include "kernel/ipc/ipc.h"
#include "kernel/process64.h"

int ipc_process_can_receive(const Process* process) {
    if (process == 0 || process->pid == 0 || !process->active) {
        return 0;
    }
    if (process->state == PROCESS_STATE_EMPTY ||
        process->state == PROCESS_STATE_RETURNED ||
        process->state == PROCESS_STATE_FAILED) {
        return 0;
    }
    return 1;
}

int ipc_validate_user_message(const OsIpcMessage* message) {
    if (message == 0) {
        return IPC_ERR_BAD_BUFFER;
    }
    if (message->size != sizeof(OsIpcMessage)) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->length > OS_IPC_MESSAGE_PAYLOAD_SIZE) {
        return IPC_ERR_MESSAGE_TOO_LARGE;
    }
    if (message->type != OS_IPC_MESSAGE_REQUEST &&
        message->type != OS_IPC_MESSAGE_REPLY &&
        message->type != OS_IPC_MESSAGE_EVENT) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if ((message->flags & ~OS_IPC_FLAG_REQUEST_REPLY) != 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->type != OS_IPC_MESSAGE_REQUEST &&
        (message->flags & OS_IPC_FLAG_REQUEST_REPLY) != 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    return IPC_OK;
}

int ipc_prepare_message(uint32_t sender_pid, const OsIpcMessage* source, OsIpcMessage* prepared) {
    if (prepared == 0) {
        return IPC_ERR_BAD_BUFFER;
    }

    int result = ipc_validate_user_message(source);
    if (result != IPC_OK) {
        return result;
    }

    *prepared = *source;
    prepared->size = sizeof(OsIpcMessage);
    prepared->sender_pid = sender_pid;
    prepared->reserved = 0;
    for (uint32_t i = prepared->length; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        prepared->payload[i] = 0;
    }
    return IPC_OK;
}

int ipc_send_message(Process* sender, Process* target, const OsIpcMessage* message) {
    if (sender == 0 || sender->pid == 0 || !sender->active) {
        return IPC_ERR_NOT_READY;
    }
    if (!ipc_process_can_receive(target)) {
        return IPC_ERR_NO_TARGET;
    }
    if (sender->pid == target->pid) {
        return IPC_ERR_PERMISSION_DENIED;
    }

    OsIpcMessage prepared;
    int result = ipc_prepare_message(sender->pid, message, &prepared);
    if (result != IPC_OK) {
        return result;
    }
    if (!process_ipc_mailbox_push(target, &prepared)) {
        return IPC_ERR_QUEUE_FULL;
    }
    process_wait_signal(target, PROCESS_WAIT_IPC, PROCESS_WAIT_OK);
    return IPC_OK;
}

int ipc_receive_message(Process* receiver, OsIpcMessage* message) {
    if (!ipc_process_can_receive(receiver)) {
        return IPC_ERR_NOT_READY;
    }
    if (message == 0) {
        return IPC_ERR_BAD_BUFFER;
    }
    if (!process_ipc_mailbox_pop(receiver, message)) {
        return IPC_ERR_WOULD_BLOCK;
    }
    return IPC_OK;
}
