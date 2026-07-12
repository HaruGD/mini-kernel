#include "kernel/ipc/ipc_mailbox.h"
#include "kernel/fault_injection.h"

static void clear_message_v2(OsIpcMessageV2* message) {
    if (message == 0) {
        return;
    }
    message->size = sizeof(OsIpcMessageV2);
    message->abi_version = OS64_IPC_ABI_VERSION_V2;
    message->sender_pid = 0;
    message->sender_generation = 0;
    message->type = OS_IPC_MESSAGE_NONE;
    message->flags = OS_IPC_FLAG_NONE;
    message->length = 0;
    message->request_id = 0;
    message->reply_to = 0;
    message->handle_count = 0;
    for (uint32_t i = 0; i < OS_IPC_V2_MAX_HANDLES; i++) {
        message->handles[i] = 0;
    }
    for (uint32_t i = 0; i < OS_IPC_V2_MESSAGE_PAYLOAD_SIZE; i++) {
        message->payload[i] = 0;
    }
}

static int message_shape_valid_v2(const OsIpcMessageV2* message) {
    if (message == 0) {
        return 0;
    }
    if (message->size != sizeof(OsIpcMessageV2) ||
        message->abi_version != OS64_IPC_ABI_VERSION_V2) {
        return 0;
    }
    if (message->length > OS_IPC_V2_MESSAGE_PAYLOAD_SIZE ||
        message->handle_count > OS_IPC_V2_MAX_HANDLES) {
        return 0;
    }
    return 1;
}

static void message_v1_to_v2(const OsIpcMessage* source, OsIpcMessageV2* target) {
    clear_message_v2(target);
    if (source == 0 || target == 0) {
        return;
    }
    target->sender_pid = source->sender_pid;
    target->sender_generation = source->sender_generation;
    target->type = source->type;
    target->flags = source->flags & OS_IPC_FLAG_REQUEST_REPLY;
    target->length = source->length;
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        target->payload[i] = source->payload[i];
    }
}

static void message_v2_to_v1(const OsIpcMessageV2* source, OsIpcMessage* target) {
    if (target == 0) {
        return;
    }
    target->size = sizeof(OsIpcMessage);
    target->sender_pid = source != 0 ? source->sender_pid : 0;
    target->type = source != 0 ? source->type : OS_IPC_MESSAGE_NONE;
    target->flags = source != 0 ? (source->flags & OS_IPC_FLAG_REQUEST_REPLY) : OS_IPC_FLAG_NONE;
    target->length = source != 0 && source->length < OS_IPC_MESSAGE_PAYLOAD_SIZE
        ? source->length
        : OS_IPC_MESSAGE_PAYLOAD_SIZE;
    target->sender_generation = source != 0 ? source->sender_generation : 0;
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        target->payload[i] = source != 0 ? source->payload[i] : 0;
    }
}

static int filter_matches(const OsIpcReceiveFilter* filter, const OsIpcMessageV2* message) {
    if (filter == 0 || filter->flags == 0) {
        return 1;
    }
    if (filter->size != sizeof(OsIpcReceiveFilter)) {
        return 0;
    }
    if ((filter->flags & OS_IPC_FILTER_SENDER) != 0 &&
        (message->sender_pid != filter->sender_pid ||
         message->sender_generation != filter->sender_generation)) {
        return 0;
    }
    if ((filter->flags & OS_IPC_FILTER_TYPE) != 0 &&
        message->type != filter->type) {
        return 0;
    }
    if ((filter->flags & OS_IPC_FILTER_REPLY_TO) != 0 &&
        message->reply_to != filter->reply_to) {
        return 0;
    }
    return (filter->flags & ~(OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE | OS_IPC_FILTER_REPLY_TO)) == 0;
}

void ipc_mailbox_init(KernelIpcMailbox* mailbox) {
    if (mailbox == 0) {
        return;
    }
    kernel_spinlock_init(&mailbox->lock, KERNEL_LOCK_CLASS_IPC_SERVICE, "ipc_mailbox");
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->count = 0;
    mailbox->delivered_count = 0;
    mailbox->dropped_count = 0;
    for (uint32_t i = 0; i < IPC_MAILBOX_CAPACITY; i++) {
        clear_message_v2(&mailbox->messages[i]);
    }
}

uint32_t ipc_mailbox_capacity(const KernelIpcMailbox* mailbox) {
    return mailbox == 0 ? 0 : IPC_MAILBOX_CAPACITY;
}

uint32_t ipc_mailbox_count(const KernelIpcMailbox* mailbox) {
    KernelIpcMailboxStats stats;
    ipc_mailbox_get_stats(mailbox, &stats);
    return stats.count;
}

uint32_t ipc_mailbox_delivered_count(const KernelIpcMailbox* mailbox) {
    KernelIpcMailboxStats stats;
    ipc_mailbox_get_stats(mailbox, &stats);
    return stats.delivered_count;
}

uint32_t ipc_mailbox_dropped_count(const KernelIpcMailbox* mailbox) {
    KernelIpcMailboxStats stats;
    ipc_mailbox_get_stats(mailbox, &stats);
    return stats.dropped_count;
}

void ipc_mailbox_get_stats(const KernelIpcMailbox* mailbox, KernelIpcMailboxStats* stats) {
    if (stats == 0) {
        return;
    }
    stats->capacity = IPC_MAILBOX_CAPACITY;
    stats->count = 0;
    stats->delivered_count = 0;
    stats->dropped_count = 0;
    if (mailbox == 0) {
        return;
    }
    KernelSpinlockToken token;
    KernelIpcMailbox* mutable_mailbox = (KernelIpcMailbox*)mailbox;
    if (!kernel_spinlock_acquire(&mutable_mailbox->lock, &token)) {
        return;
    }
    stats->count = mailbox->count;
    stats->delivered_count = mailbox->delivered_count;
    stats->dropped_count = mailbox->dropped_count;
    kernel_spinlock_release(&mutable_mailbox->lock, &token);
}

int ipc_mailbox_is_empty(const KernelIpcMailbox* mailbox) {
    return ipc_mailbox_count(mailbox) == 0;
}

int ipc_mailbox_is_full(const KernelIpcMailbox* mailbox) {
    return ipc_mailbox_count(mailbox) == IPC_MAILBOX_CAPACITY;
}

int ipc_mailbox_push_v2(KernelIpcMailbox* mailbox, const OsIpcMessageV2* message) {
    if (mailbox == 0 || !message_shape_valid_v2(message)) {
        return 0;
    }
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_MAILBOX)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mailbox->lock, &token)) {
        return 0;
    }
    if (mailbox->count == IPC_MAILBOX_CAPACITY) {
        mailbox->dropped_count++;
        kernel_spinlock_release(&mailbox->lock, &token);
        return 0;
    }

    mailbox->messages[mailbox->tail] = *message;
    mailbox->tail = (mailbox->tail + 1u) % IPC_MAILBOX_CAPACITY;
    mailbox->count++;
    kernel_spinlock_release(&mailbox->lock, &token);
    return 1;
}

int ipc_mailbox_pop_v2(KernelIpcMailbox* mailbox, OsIpcMessageV2* message) {
    if (mailbox == 0 || message == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mailbox->lock, &token)) {
        return 0;
    }
    if (mailbox->count == 0) {
        kernel_spinlock_release(&mailbox->lock, &token);
        return 0;
    }

    *message = mailbox->messages[mailbox->head];
    clear_message_v2(&mailbox->messages[mailbox->head]);
    mailbox->head = (mailbox->head + 1u) % IPC_MAILBOX_CAPACITY;
    mailbox->count--;
    mailbox->delivered_count++;
    kernel_spinlock_release(&mailbox->lock, &token);
    return 1;
}

int ipc_mailbox_pop_v2_match(KernelIpcMailbox* mailbox,
                             const OsIpcReceiveFilter* filter,
                             OsIpcMessageV2* message) {
    if (mailbox == 0 || message == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mailbox->lock, &token)) {
        return 0;
    }
    if (mailbox->count == 0) {
        kernel_spinlock_release(&mailbox->lock, &token);
        return 0;
    }

    uint32_t found_offset = IPC_MAILBOX_CAPACITY;
    for (uint32_t i = 0; i < mailbox->count; i++) {
        uint32_t index = (mailbox->head + i) % IPC_MAILBOX_CAPACITY;
        if (filter_matches(filter, &mailbox->messages[index])) {
            found_offset = i;
            break;
        }
    }
    if (found_offset == IPC_MAILBOX_CAPACITY) {
        kernel_spinlock_release(&mailbox->lock, &token);
        return 0;
    }

    for (uint32_t i = 0; i < found_offset; i++) {
        OsIpcMessageV2 rotated = mailbox->messages[mailbox->head];
        clear_message_v2(&mailbox->messages[mailbox->head]);
        mailbox->head = (mailbox->head + 1u) % IPC_MAILBOX_CAPACITY;
        mailbox->messages[mailbox->tail] = rotated;
        mailbox->tail = (mailbox->tail + 1u) % IPC_MAILBOX_CAPACITY;
    }
    *message = mailbox->messages[mailbox->head];
    clear_message_v2(&mailbox->messages[mailbox->head]);
    mailbox->head = (mailbox->head + 1u) % IPC_MAILBOX_CAPACITY;
    mailbox->count--;
    mailbox->delivered_count++;
    kernel_spinlock_release(&mailbox->lock, &token);
    return 1;
}

int ipc_mailbox_push(KernelIpcMailbox* mailbox, const OsIpcMessage* message) {
    if (message == 0 || message->size != sizeof(OsIpcMessage) ||
        message->length > OS_IPC_MESSAGE_PAYLOAD_SIZE) {
        return 0;
    }

    OsIpcMessageV2 converted;
    message_v1_to_v2(message, &converted);
    return ipc_mailbox_push_v2(mailbox, &converted);
}

int ipc_mailbox_pop(KernelIpcMailbox* mailbox, OsIpcMessage* message) {
    OsIpcMessageV2 converted;
    if (!ipc_mailbox_pop_v2(mailbox, &converted)) {
        return 0;
    }
    message_v2_to_v1(&converted, message);
    return 1;
}

void ipc_mailbox_drop_all(KernelIpcMailbox* mailbox) {
    if (mailbox == 0) {
        return;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mailbox->lock, &token)) {
        return;
    }
    mailbox->dropped_count += mailbox->count;
    for (uint32_t i = 0; i < IPC_MAILBOX_CAPACITY; i++) {
        clear_message_v2(&mailbox->messages[i]);
    }
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->count = 0;
    kernel_spinlock_release(&mailbox->lock, &token);
}
