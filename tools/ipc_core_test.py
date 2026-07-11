#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/handle/kernel_handle.h"
#include "kernel/handle/kernel_objects.h"
#include "kernel/ipc/ipc.h"
#include "kernel/process64.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static OsIpcMessage make_message(uint32_t type, uint32_t sequence) {
    OsIpcMessage message;
    message.size = sizeof(OsIpcMessage);
    message.sender_pid = 0xABC00000u | sequence;
    message.type = type;
    message.flags = type == OS_IPC_MESSAGE_REQUEST ? OS_IPC_FLAG_REQUEST_REPLY : OS_IPC_FLAG_NONE;
    message.length = 8;
    message.sender_generation = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        message.payload[i] = (uint8_t)(sequence + i);
    }
    return message;
}

static OsIpcMessageV2 make_message_v2(uint32_t type, uint32_t sequence) {
    OsIpcMessageV2 message;
    message.size = sizeof(OsIpcMessageV2);
    message.abi_version = OS64_IPC_ABI_VERSION_V2;
    message.sender_pid = 0xCC000000u | sequence;
    message.sender_generation = 0xFFFFFFFFu;
    message.type = type;
    message.flags = type == OS_IPC_MESSAGE_REQUEST ? OS_IPC_FLAG_REQUEST_REPLY : OS_IPC_FLAG_NONE;
    message.length = OS_IPC_V2_MESSAGE_PAYLOAD_SIZE;
    message.request_id = sequence;
    message.reply_to = type == OS_IPC_MESSAGE_REPLY ? sequence - 1u : 0;
    message.handle_count = 0;
    for (uint32_t i = 0; i < OS_IPC_V2_MAX_HANDLES; i++) {
        message.handles[i] = 0;
    }
    for (uint32_t i = 0; i < OS_IPC_V2_MESSAGE_PAYLOAD_SIZE; i++) {
        message.payload[i] = (uint8_t)(sequence + i);
    }
    return message;
}

static void init_process(Process* process, uint32_t pid) {
    process_clear(process);
    process->pid = pid;
    process->active = 1;
    process->state = PROCESS_STATE_RUNNING;
}

static void clear_process_table() {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
    process_clear_focus(0);
}

int main() {
    clear_process_table();
    kernel_objects_init();
    Process* sender = &process_table[0];
    Process* target = &process_table[1];
    init_process(sender, 10);
    init_process(target, 20);

    OsIpcMessage message = make_message(OS_IPC_MESSAGE_REQUEST, 1);
    check(ipc_validate_user_message(&message) == IPC_OK);
    check(ipc_send_message(sender, target, &message) == IPC_OK);
    check(process_ipc_mailbox_count(target) == 1);

    OsIpcMessage received;
    check(ipc_receive_message(target, &received) == IPC_OK);
    check(received.sender_pid == sender->pid);
    check(received.type == OS_IPC_MESSAGE_REQUEST);
    check(received.flags == OS_IPC_FLAG_REQUEST_REPLY);
    check(received.length == 8);
    check(received.sender_generation == sender->generation);
    for (uint32_t i = received.length; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        check(received.payload[i] == 0);
    }
    check(process_ipc_mailbox_delivered_count(target) == 1);
    check(ipc_receive_message(target, &received) == IPC_ERR_WOULD_BLOCK);

    message = make_message(OS_IPC_MESSAGE_REPLY, 2);
    check(ipc_send_message(target, sender, &message) == IPC_OK);
    check(ipc_receive_message(sender, &received) == IPC_OK);
    check(received.sender_pid == target->pid);
    check(received.type == OS_IPC_MESSAGE_REPLY);
    check(received.flags == OS_IPC_FLAG_NONE);

    message = make_message(OS_IPC_MESSAGE_EVENT, 3);
    check(ipc_send_message(sender, target, &message) == IPC_OK);
    check(ipc_receive_message(target, &received) == IPC_OK);
    check(received.type == OS_IPC_MESSAGE_EVENT);

    message = make_message(OS_IPC_MESSAGE_REQUEST, 4);
    check(ipc_send_message(sender, 0, &message) == IPC_ERR_NO_TARGET);
    check(ipc_send_message(0, target, &message) == IPC_ERR_NOT_READY);
    check(ipc_send_message(sender, sender, &message) == IPC_ERR_PERMISSION_DENIED);

    target->active = 0;
    check(ipc_send_message(sender, target, &message) == IPC_ERR_NO_TARGET);
    target->active = 1;
    target->state = PROCESS_STATE_RETURNED;
    check(ipc_send_message(sender, target, &message) == IPC_ERR_NO_TARGET);
    target->state = PROCESS_STATE_RUNNING;

    message.size = sizeof(OsIpcMessage) - 1u;
    check(ipc_send_message(sender, target, &message) == IPC_ERR_INVALID_ARGUMENT);
    message = make_message(OS_IPC_MESSAGE_REQUEST, 5);
    message.length = OS_IPC_MESSAGE_PAYLOAD_SIZE + 1u;
    check(ipc_send_message(sender, target, &message) == IPC_ERR_MESSAGE_TOO_LARGE);
    message = make_message(OS_IPC_MESSAGE_NONE, 6);
    check(ipc_send_message(sender, target, &message) == IPC_ERR_INVALID_ARGUMENT);
    message = make_message(OS_IPC_MESSAGE_REPLY, 7);
    message.flags = OS_IPC_FLAG_REQUEST_REPLY;
    check(ipc_send_message(sender, target, &message) == IPC_ERR_INVALID_ARGUMENT);
    message = make_message(OS_IPC_MESSAGE_REQUEST, 8);
    message.flags = 0x80000000u;
    check(ipc_send_message(sender, target, &message) == IPC_ERR_INVALID_ARGUMENT);

    OsIpcMessageV2 message_v2 = make_message_v2(OS_IPC_MESSAGE_REQUEST, 1000);
    check(ipc_validate_user_message_v2(&message_v2) == IPC_OK);
    check(ipc_send_message_v2(sender, target, &message_v2) == IPC_OK);
    OsIpcMessageV2 received_v2;
    check(ipc_receive_message_v2(target, &received_v2) == IPC_OK);
    check(received_v2.size == sizeof(OsIpcMessageV2));
    check(received_v2.abi_version == OS64_IPC_ABI_VERSION_V2);
    check(received_v2.sender_pid == sender->pid);
    check(received_v2.sender_generation == sender->generation);
    check(received_v2.request_id == 1000);
    check(received_v2.length == OS_IPC_V2_MESSAGE_PAYLOAD_SIZE);
    check(received_v2.payload[80] == (uint8_t)(1000u + 80u));

    OsIpcMessageV2 unrelated = make_message_v2(OS_IPC_MESSAGE_EVENT, 2000);
    OsIpcMessageV2 reply = make_message_v2(OS_IPC_MESSAGE_REPLY, 1001);
    reply.reply_to = 1000;
    check(ipc_send_message_v2(sender, target, &unrelated) == IPC_OK);
    check(ipc_send_message_v2(sender, target, &reply) == IPC_OK);
    OsIpcReceiveFilter filter;
    filter.size = sizeof(OsIpcReceiveFilter);
    filter.flags = OS_IPC_FILTER_SENDER | OS_IPC_FILTER_TYPE | OS_IPC_FILTER_REPLY_TO;
    filter.sender_pid = sender->pid;
    filter.sender_generation = sender->generation;
    filter.type = OS_IPC_MESSAGE_REPLY;
    filter.reply_to = 1000;
    check(ipc_receive_message_v2_match(target, &filter, &received_v2) == IPC_OK);
    check(received_v2.type == OS_IPC_MESSAGE_REPLY);
    check(received_v2.reply_to == 1000);
    check(ipc_receive_message_v2(target, &received_v2) == IPC_OK);
    check(received_v2.type == OS_IPC_MESSAGE_EVENT);

    message_v2 = make_message_v2(OS_IPC_MESSAGE_EVENT, 3000);
    message_v2.flags = OS_IPC_FLAG_HAS_HANDLES;
    message_v2.handle_count = 1;
    message_v2.handles[0] = kernel_handle_alloc(&sender->handle_table,
                                                KERNEL_HANDLE_TYPE_VFS_FILE,
                                                KERNEL_HANDLE_RIGHT_READ,
                                                77,
                                                0);
    check(message_v2.handles[0] != 0);
    check(ipc_send_message_v2(sender, target, &message_v2) == IPC_ERR_PERMISSION_DENIED);
    check(kernel_handle_count_type(&target->handle_table, KERNEL_HANDLE_TYPE_VFS_FILE) == 0);

    message_v2.handles[0] = kernel_shared_memory_create(&sender->handle_table,
                                                        sender->pid,
                                                        4096,
                                                        KERNEL_HANDLE_RIGHT_READ |
                                                        KERNEL_HANDLE_RIGHT_WRITE |
                                                        KERNEL_HANDLE_RIGHT_MAP |
                                                        KERNEL_HANDLE_RIGHT_TRANSFER);
    check(message_v2.handles[0] != 0);
    check(ipc_send_message_v2(sender, target, &message_v2) == IPC_OK);
    check(ipc_receive_message_v2(target, &received_v2) == IPC_OK);
    check(received_v2.handle_count == 1);
    check(received_v2.handles[0] != 0);
    check(kernel_handle_resolve(&target->handle_table,
                                received_v2.handles[0],
                                KERNEL_HANDLE_TYPE_SHARED_MEMORY,
                                KERNEL_HANDLE_RIGHT_MAP) != 0);

    for (uint32_t i = 0; i < 100000u; i++) {
        message_v2 = make_message_v2(OS_IPC_MESSAGE_EVENT, 4000u + i);
        message_v2.length = 12;
        check(ipc_send_message_v2(sender, target, &message_v2) == IPC_OK);
        check(ipc_receive_message_v2(target, &received_v2) == IPC_OK);
        check(received_v2.request_id == 4000u + i);
    }
    check(process_ipc_mailbox_count(target) == 0);

    for (uint32_t i = 0; i < IPC_MAILBOX_CAPACITY; i++) {
        message = make_message(OS_IPC_MESSAGE_EVENT, 100u + i);
        check(ipc_send_message(sender, target, &message) == IPC_OK);
    }
    message = make_message(OS_IPC_MESSAGE_EVENT, 999);
    check(ipc_send_message(sender, target, &message) == IPC_ERR_QUEUE_FULL);
    check(process_ipc_mailbox_count(target) == IPC_MAILBOX_CAPACITY);
    check(process_ipc_mailbox_dropped_count(target) == 1);

    process_ipc_wait_begin(target);
    check(process_ipc_waiting(target) == 1);
    process_mark_returned(target, PROCESS_TERM_EXIT, 0);
    check(process_ipc_waiting(target) == 0);
    check(process_ipc_mailbox_count(target) == 0);
    check(process_ipc_mailbox_dropped_count(target) == 0);
    check(ipc_receive_message(target, &received) == IPC_ERR_NOT_READY);

    init_process(target, 20);
    message = make_message(OS_IPC_MESSAGE_EVENT, 200);
    check(ipc_send_message(sender, target, &message) == IPC_OK);
    process_ipc_wait_begin(target);
    check(process_ipc_waiting(target) == 1);
    process_mark_failed(target, PROCESS_TERM_KILLED, 9);
    check(process_ipc_waiting(target) == 0);
    check(process_ipc_mailbox_count(target) == 0);
    check(ipc_receive_message(target, &received) == IPC_ERR_NOT_READY);

    clear_process_table();
    target = &process_table[1];
    init_process(target, 30);
    target->state = PROCESS_STATE_PAUSED;
    target->resumable = 1;

    check(process_wait_begin(target, PROCESS_WAIT_IPC, 0x12340000u, 5, 100) == 1);
    check(process_wait_is_pending(target) == 1);
    check(target->wait_reason == PROCESS_WAIT_IPC);
    check(target->wait_user_address == 0x12340000u);
    check(target->wait_deadline == 105);
    check(target->scheduler_state == SCHED_STATE_WAITING);
    check(process_wait_begin(target, PROCESS_WAIT_INPUT, 0, 0, 0) == 0);
    check(process_wait_signal(target, PROCESS_WAIT_INPUT, PROCESS_WAIT_OK) == 0);
    process_wait_tick(104);
    check(process_wait_is_pending(target) == 1);
    process_wait_tick(105);
    check(process_wait_is_pending(target) == 0);
    check(target->wait_result == PROCESS_WAIT_TIMEOUT);
    check(target->scheduler_state == SCHED_STATE_READY);
    check(process_wait_signal(target, PROCESS_WAIT_IPC, PROCESS_WAIT_OK) == 0);

    check(process_wait_begin(target, PROCESS_WAIT_INPUT, 0x56780000u, 0, 0) == 1);
    check(target->wait_has_deadline == 0);
    check(process_wait_cancel(target, PROCESS_WAIT_INPUT, PROCESS_WAIT_CANCELLED) == 1);
    check(process_wait_is_pending(target) == 0);
    check(target->wait_result == PROCESS_WAIT_CANCELLED);
    check(target->scheduler_state == SCHED_STATE_READY);

    check(process_wait_begin(target, PROCESS_WAIT_TIMER, 0, 3, 0xFFFFFFFEu) == 1);
    check(target->wait_deadline == 1);
    process_wait_tick(0);
    check(process_wait_is_pending(target) == 1);
    process_wait_tick(1);
    check(process_wait_is_pending(target) == 0);
    check(target->wait_result == PROCESS_WAIT_OK);
    check(target->scheduler_state == SCHED_STATE_READY);
    process_wait_reset(target);
    check(target->wait_reason == PROCESS_WAIT_NONE);
    check(target->wait_user_address == 0);

    clear_process_table();
    return failures == 0 ? 0 : 1;
}
"""


STUB_SOURCE = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

uint32_t vfs_close_all_for_owner(uint32_t) {
    return 0;
}

extern "C" void* kmalloc(size_t size) {
    return malloc(size);
}

extern "C" void kfree(void* pointer) {
    free(pointer);
}

void copy_string64(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_ipc_core_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "ipc_core_test.cpp"
        stub_path = temp_path / "ipc_core_stubs.cpp"
        binary_path = temp_path / "ipc_core_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        stub_path.write_text(textwrap.dedent(STUB_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DOS64_HOST_TEST",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/sync/spinlock.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_objects.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(REPO_ROOT / "kernel/ipc/ipc.cpp"),
            str(REPO_ROOT / "kernel/ipc/ipc_mailbox.cpp"),
            str(REPO_ROOT / "kernel/input/input_event_queue.cpp"),
            str(REPO_ROOT / "kernel/process/process64.cpp"),
            str(REPO_ROOT / "kernel/service/service_registry.cpp"),
            str(source_path),
            str(stub_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("ipc core test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
