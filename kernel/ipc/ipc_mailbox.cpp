#include "kernel/ipc/ipc_mailbox.h"

static void clear_message(OsIpcMessage* message) {
    if (message == 0) {
        return;
    }
    message->size = sizeof(OsIpcMessage);
    message->sender_pid = 0;
    message->type = OS_IPC_MESSAGE_NONE;
    message->flags = OS_IPC_FLAG_NONE;
    message->length = 0;
    message->reserved = 0;
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        message->payload[i] = 0;
    }
}

static int message_shape_valid(const OsIpcMessage* message) {
    if (message == 0) {
        return 0;
    }
    if (message->size != sizeof(OsIpcMessage)) {
        return 0;
    }
    if (message->length > OS_IPC_MESSAGE_PAYLOAD_SIZE) {
        return 0;
    }
    return 1;
}

void ipc_mailbox_init(KernelIpcMailbox* mailbox) {
    if (mailbox == 0) {
        return;
    }
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->count = 0;
    mailbox->delivered_count = 0;
    mailbox->dropped_count = 0;
    for (uint32_t i = 0; i < IPC_MAILBOX_CAPACITY; i++) {
        clear_message(&mailbox->messages[i]);
    }
}

uint32_t ipc_mailbox_capacity(const KernelIpcMailbox* mailbox) {
    return mailbox == 0 ? 0 : IPC_MAILBOX_CAPACITY;
}

uint32_t ipc_mailbox_count(const KernelIpcMailbox* mailbox) {
    return mailbox == 0 ? 0 : mailbox->count;
}

uint32_t ipc_mailbox_delivered_count(const KernelIpcMailbox* mailbox) {
    return mailbox == 0 ? 0 : mailbox->delivered_count;
}

uint32_t ipc_mailbox_dropped_count(const KernelIpcMailbox* mailbox) {
    return mailbox == 0 ? 0 : mailbox->dropped_count;
}

int ipc_mailbox_is_empty(const KernelIpcMailbox* mailbox) {
    return mailbox == 0 || mailbox->count == 0;
}

int ipc_mailbox_is_full(const KernelIpcMailbox* mailbox) {
    return mailbox != 0 && mailbox->count == IPC_MAILBOX_CAPACITY;
}

int ipc_mailbox_push(KernelIpcMailbox* mailbox, const OsIpcMessage* message) {
    if (mailbox == 0 || !message_shape_valid(message)) {
        return 0;
    }
    if (ipc_mailbox_is_full(mailbox)) {
        mailbox->dropped_count++;
        return 0;
    }

    mailbox->messages[mailbox->tail] = *message;
    mailbox->tail = (mailbox->tail + 1u) % IPC_MAILBOX_CAPACITY;
    mailbox->count++;
    return 1;
}

int ipc_mailbox_pop(KernelIpcMailbox* mailbox, OsIpcMessage* message) {
    if (mailbox == 0 || message == 0 || ipc_mailbox_is_empty(mailbox)) {
        return 0;
    }

    *message = mailbox->messages[mailbox->head];
    clear_message(&mailbox->messages[mailbox->head]);
    mailbox->head = (mailbox->head + 1u) % IPC_MAILBOX_CAPACITY;
    mailbox->count--;
    mailbox->delivered_count++;
    return 1;
}

void ipc_mailbox_drop_all(KernelIpcMailbox* mailbox) {
    if (mailbox == 0) {
        return;
    }
    mailbox->dropped_count += mailbox->count;
    for (uint32_t i = 0; i < IPC_MAILBOX_CAPACITY; i++) {
        clear_message(&mailbox->messages[i]);
    }
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->count = 0;
}
