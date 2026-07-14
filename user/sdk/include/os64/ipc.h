#ifndef OS64_IPC_H
#define OS64_IPC_H

#include <stdint.h>
#include "os64/ipc_types.h"
#include "os64/process_types.h"

void os_msg_init(OsIpcMessage* message, uint32_t type);
void os_msg_v2_init(OsIpcMessageV2* message, uint32_t type);
void os_ipc_filter_init(OsIpcReceiveFilter* filter);
long os_msg_send(uint32_t target_pid, const OsIpcMessage* message);
long os_msg_send_to_identity(OsProcessIdentity target, const OsIpcMessage* message);
long os_msg_recv(OsIpcMessage* message);
long os_msg_wait(OsIpcMessage* message);
long os_msg_wait_timeout(OsIpcMessage* message, uint32_t timeout_ticks);
long os_ipc_features(uint32_t* abi_version_out, uint32_t* features_out);
uint32_t os_msg_next_request_id(void);
long os_msg_v2_send_to_identity(OsProcessIdentity target, const OsIpcMessageV2* message);
long os_msg_v2_recv(OsIpcMessageV2* message);
long os_msg_v2_wait(OsIpcMessageV2* message);
long os_msg_v2_wait_timeout(OsIpcMessageV2* message, uint32_t timeout_ticks);
long os_msg_v2_recv_match(const OsIpcReceiveFilter* filter, OsIpcMessageV2* message);
long os_msg_v2_request(OsProcessIdentity target,
                       OsIpcMessageV2* request,
                       OsIpcMessageV2* reply,
                       uint32_t timeout_ticks);
OsProcessIdentity os_msg_sender_identity(const OsIpcMessage* message);
OsProcessIdentity os_msg_v2_sender_identity(const OsIpcMessageV2* message);

#endif
