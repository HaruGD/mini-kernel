#ifndef KERNEL_IPC_MAILBOX_H
#define KERNEL_IPC_MAILBOX_H

#include <stdint.h>
#include "os64/ipc_types.h"

#define IPC_MAILBOX_CAPACITY 16u

typedef struct KernelIpcMailbox {
    OsIpcMessage messages[IPC_MAILBOX_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t delivered_count;
    uint32_t dropped_count;
} KernelIpcMailbox;

#ifdef __cplusplus
extern "C" {
#endif

void ipc_mailbox_init(KernelIpcMailbox* mailbox);
uint32_t ipc_mailbox_capacity(const KernelIpcMailbox* mailbox);
uint32_t ipc_mailbox_count(const KernelIpcMailbox* mailbox);
uint32_t ipc_mailbox_delivered_count(const KernelIpcMailbox* mailbox);
uint32_t ipc_mailbox_dropped_count(const KernelIpcMailbox* mailbox);
int ipc_mailbox_is_empty(const KernelIpcMailbox* mailbox);
int ipc_mailbox_is_full(const KernelIpcMailbox* mailbox);
int ipc_mailbox_push(KernelIpcMailbox* mailbox, const OsIpcMessage* message);
int ipc_mailbox_pop(KernelIpcMailbox* mailbox, OsIpcMessage* message);
void ipc_mailbox_drop_all(KernelIpcMailbox* mailbox);

#ifdef __cplusplus
}
#endif

#endif
