#ifndef OS64_IPC_H
#define OS64_IPC_H

#include <stdint.h>
#include "os64/ipc_types.h"

void os_msg_init(OsIpcMessage* message, uint32_t type);
long os_msg_send(uint32_t target_pid, const OsIpcMessage* message);
long os_msg_recv(OsIpcMessage* message);
long os_msg_wait(OsIpcMessage* message);

#endif
