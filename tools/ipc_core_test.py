#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
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
    message.reserved = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
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
    check(received.reserved == 0);
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
    return failures == 0 ? 0 : 1;
}
"""


STUB_SOURCE = r"""
#include <stdint.h>

uint32_t vfs_close_all_for_owner(uint32_t) {
    return 0;
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
            "-I",
            str(REPO_ROOT / "include"),
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
