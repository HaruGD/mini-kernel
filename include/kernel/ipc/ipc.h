#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stdint.h>

#include "kernel/process.h"
#include "os64/ipc_types.h"

#define IPC_OK 0
#define IPC_ERR_NOT_READY (-1)
#define IPC_ERR_INVALID_ARGUMENT (-2)
#define IPC_ERR_WOULD_BLOCK (-9)
#define IPC_ERR_NO_TARGET (-12)
#define IPC_ERR_QUEUE_FULL (-13)
#define IPC_ERR_MESSAGE_TOO_LARGE (-14)
#define IPC_ERR_PERMISSION_DENIED (-15)
#define IPC_ERR_BAD_BUFFER (-16)

#ifdef __cplusplus
extern "C" {
#endif

int ipc_process_can_receive(const Process* process);
int ipc_validate_user_message(const OsIpcMessage* message);
int ipc_prepare_message(uint32_t sender_pid, const OsIpcMessage* source, OsIpcMessage* prepared);
int ipc_send_message(Process* sender, Process* target, const OsIpcMessage* message);
int ipc_receive_message(Process* receiver, OsIpcMessage* message);

#ifdef __cplusplus
}
#endif

#endif
