#include "kernel/ipc/ipc.h"
#include "kernel/handle/kernel_handle.h"
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

int ipc_validate_user_message_v2(const OsIpcMessageV2* message) {
    if (message == 0) {
        return IPC_ERR_BAD_BUFFER;
    }
    if (message->size != sizeof(OsIpcMessageV2) ||
        message->abi_version != OS64_IPC_ABI_VERSION_V2) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->length > OS_IPC_V2_MESSAGE_PAYLOAD_SIZE) {
        return IPC_ERR_MESSAGE_TOO_LARGE;
    }
    if (message->handle_count > OS_IPC_V2_MAX_HANDLES) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->type != OS_IPC_MESSAGE_REQUEST &&
        message->type != OS_IPC_MESSAGE_REPLY &&
        message->type != OS_IPC_MESSAGE_EVENT) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if ((message->flags & ~(OS_IPC_FLAG_REQUEST_REPLY | OS_IPC_FLAG_HAS_HANDLES)) != 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->type != OS_IPC_MESSAGE_REQUEST &&
        (message->flags & OS_IPC_FLAG_REQUEST_REPLY) != 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->handle_count == 0 && (message->flags & OS_IPC_FLAG_HAS_HANDLES) != 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (message->handle_count != 0 && (message->flags & OS_IPC_FLAG_HAS_HANDLES) == 0) {
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
    prepared->sender_generation = 0;
    for (uint32_t i = prepared->length; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        prepared->payload[i] = 0;
    }
    return IPC_OK;
}

static void rollback_transferred_handles(Process* target, uint64_t* handles, uint32_t count) {
    if (target == 0 || handles == 0) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (handles[i] != 0) {
            kernel_handle_close(&target->handle_table, handles[i], 0);
            handles[i] = 0;
        }
    }
}

static int transfer_handles(Process* sender, Process* target, OsIpcMessageV2* prepared) {
    if (prepared->handle_count == 0) {
        return IPC_OK;
    }
    if (sender == 0 || target == 0) {
        return IPC_ERR_NOT_READY;
    }

    uint64_t new_handles[OS_IPC_V2_MAX_HANDLES];
    for (uint32_t i = 0; i < OS_IPC_V2_MAX_HANDLES; i++) {
        new_handles[i] = 0;
    }

    for (uint32_t i = 0; i < prepared->handle_count; i++) {
        KernelHandle* source = kernel_handle_resolve(&sender->handle_table,
                                                     prepared->handles[i],
                                                     KERNEL_HANDLE_TYPE_NONE,
                                                     KERNEL_HANDLE_RIGHT_TRANSFER);
        if (source == 0) {
            rollback_transferred_handles(target, new_handles, i);
            return IPC_ERR_PERMISSION_DENIED;
        }

        uint64_t cloned = kernel_handle_alloc(&target->handle_table,
                                              source->type,
                                              source->rights,
                                              source->object,
                                              source->extra);
        if (cloned == 0) {
            rollback_transferred_handles(target, new_handles, i);
            return IPC_ERR_QUEUE_FULL;
        }
        new_handles[i] = cloned;
    }

    for (uint32_t i = 0; i < prepared->handle_count; i++) {
        prepared->handles[i] = new_handles[i];
    }
    return IPC_OK;
}

int ipc_prepare_message_v2(Process* sender,
                           Process* target,
                           const OsIpcMessageV2* source,
                           OsIpcMessageV2* prepared) {
    if (prepared == 0) {
        return IPC_ERR_BAD_BUFFER;
    }

    int result = ipc_validate_user_message_v2(source);
    if (result != IPC_OK) {
        return result;
    }

    *prepared = *source;
    prepared->size = sizeof(OsIpcMessageV2);
    prepared->abi_version = OS64_IPC_ABI_VERSION_V2;
    prepared->sender_pid = sender != 0 ? sender->pid : 0;
    prepared->sender_generation = sender != 0 ? sender->generation : 0;
    for (uint32_t i = prepared->length; i < OS_IPC_V2_MESSAGE_PAYLOAD_SIZE; i++) {
        prepared->payload[i] = 0;
    }
    for (uint32_t i = prepared->handle_count; i < OS_IPC_V2_MAX_HANDLES; i++) {
        prepared->handles[i] = 0;
    }
    return transfer_handles(sender, target, prepared);
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
    prepared.sender_generation = sender->generation;
    if (!process_ipc_mailbox_push(target, &prepared)) {
        return IPC_ERR_QUEUE_FULL;
    }
    process_wait_signal(target, PROCESS_WAIT_IPC, PROCESS_WAIT_OK);
    return IPC_OK;
}

int ipc_send_message_v2(Process* sender, Process* target, const OsIpcMessageV2* message) {
    if (sender == 0 || sender->pid == 0 || !sender->active) {
        return IPC_ERR_NOT_READY;
    }
    if (!ipc_process_can_receive(target)) {
        return IPC_ERR_NO_TARGET;
    }
    if (sender->pid == target->pid) {
        return IPC_ERR_PERMISSION_DENIED;
    }

    OsIpcMessageV2 prepared;
    int result = ipc_prepare_message_v2(sender, target, message, &prepared);
    if (result != IPC_OK) {
        return result;
    }
    if (!process_ipc_mailbox_push_v2(target, &prepared)) {
        rollback_transferred_handles(target, prepared.handles, prepared.handle_count);
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

int ipc_receive_message_v2(Process* receiver, OsIpcMessageV2* message) {
    if (!ipc_process_can_receive(receiver)) {
        return IPC_ERR_NOT_READY;
    }
    if (message == 0) {
        return IPC_ERR_BAD_BUFFER;
    }
    if (!process_ipc_mailbox_pop_v2(receiver, message)) {
        return IPC_ERR_WOULD_BLOCK;
    }
    return IPC_OK;
}

int ipc_receive_message_v2_match(Process* receiver,
                                 const OsIpcReceiveFilter* filter,
                                 OsIpcMessageV2* message) {
    if (!ipc_process_can_receive(receiver)) {
        return IPC_ERR_NOT_READY;
    }
    if (message == 0 || filter == 0 || filter->size != sizeof(OsIpcReceiveFilter)) {
        return IPC_ERR_BAD_BUFFER;
    }
    if ((filter->flags & ~(OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE | OS_IPC_FILTER_REPLY_TO)) != 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }
    if (!ipc_mailbox_pop_v2_match(&receiver->ipc_mailbox, filter, message)) {
        return IPC_ERR_WOULD_BLOCK;
    }
    return IPC_OK;
}
