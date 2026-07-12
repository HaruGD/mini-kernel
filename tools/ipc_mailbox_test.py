#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/fault_injection.h"
#include "kernel/ipc/ipc_mailbox.h"
#include "kernel/process64.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static OsIpcMessage make_message(uint32_t sequence) {
    OsIpcMessage message;
    message.size = sizeof(OsIpcMessage);
    message.sender_pid = 100u + sequence;
    message.type = OS_IPC_MESSAGE_REQUEST;
    message.flags = (sequence & 1u) ? OS_IPC_FLAG_REQUEST_REPLY : OS_IPC_FLAG_NONE;
    message.length = 4;
    message.sender_generation = 0;
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        message.payload[i] = (uint8_t)(sequence + i);
    }
    return message;
}

static void expect_message(const OsIpcMessage* message, uint32_t sequence) {
    check(message->size == sizeof(OsIpcMessage));
    check(message->sender_pid == 100u + sequence);
    check(message->type == OS_IPC_MESSAGE_REQUEST);
    check(message->flags == ((sequence & 1u) ? OS_IPC_FLAG_REQUEST_REPLY : OS_IPC_FLAG_NONE));
    check(message->length == 4);
    check(message->sender_generation == 0);
    for (uint32_t i = 0; i < OS_IPC_MESSAGE_PAYLOAD_SIZE; i++) {
        check(message->payload[i] == (uint8_t)(sequence + i));
    }
}

static void clear_process_table() {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
    process_clear_focus(0);
}

int main() {
    kernel_fault_injection_reset();
    KernelIpcMailbox mailbox;
    OsIpcMessage message;
    ipc_mailbox_init(&mailbox);
    OsIpcMessage injected = make_message(1);
    kernel_fault_injection_arm(KERNEL_FAULT_POINT_MAILBOX, 0);
    check(ipc_mailbox_push(&mailbox, &injected) == 0);
    check(ipc_mailbox_count(&mailbox) == 0);

    check(sizeof(OsIpcMessage) == 88);
    check(ipc_mailbox_capacity(&mailbox) == IPC_MAILBOX_CAPACITY);
    check(ipc_mailbox_count(&mailbox) == 0);
    check(ipc_mailbox_is_empty(&mailbox) == 1);
    check(ipc_mailbox_is_full(&mailbox) == 0);
    check(ipc_mailbox_delivered_count(&mailbox) == 0);
    check(ipc_mailbox_dropped_count(&mailbox) == 0);
    check(ipc_mailbox_pop(&mailbox, &message) == 0);

    message = make_message(1);
    check(ipc_mailbox_push(&mailbox, &message) == 1);
    check(ipc_mailbox_count(&mailbox) == 1);
    check(ipc_mailbox_pop(&mailbox, &message) == 1);
    expect_message(&message, 1);
    check(ipc_mailbox_is_empty(&mailbox) == 1);
    check(ipc_mailbox_delivered_count(&mailbox) == 1);

    for (uint32_t i = 0; i < IPC_MAILBOX_CAPACITY; i++) {
        message = make_message(10u + i);
        check(ipc_mailbox_push(&mailbox, &message) == 1);
    }
    check(ipc_mailbox_count(&mailbox) == IPC_MAILBOX_CAPACITY);
    check(ipc_mailbox_is_full(&mailbox) == 1);
    message = make_message(999);
    check(ipc_mailbox_push(&mailbox, &message) == 0);
    check(ipc_mailbox_count(&mailbox) == IPC_MAILBOX_CAPACITY);
    check(ipc_mailbox_dropped_count(&mailbox) == 1);

    for (uint32_t i = 0; i < 4; i++) {
        check(ipc_mailbox_pop(&mailbox, &message) == 1);
        expect_message(&message, 10u + i);
    }
    for (uint32_t i = 0; i < 4; i++) {
        message = make_message(100u + i);
        check(ipc_mailbox_push(&mailbox, &message) == 1);
    }
    for (uint32_t i = 4; i < IPC_MAILBOX_CAPACITY; i++) {
        check(ipc_mailbox_pop(&mailbox, &message) == 1);
        expect_message(&message, 10u + i);
    }
    for (uint32_t i = 0; i < 4; i++) {
        check(ipc_mailbox_pop(&mailbox, &message) == 1);
        expect_message(&message, 100u + i);
    }
    check(ipc_mailbox_is_empty(&mailbox) == 1);

    message = make_message(2);
    message.length = OS_IPC_MESSAGE_PAYLOAD_SIZE + 1u;
    check(ipc_mailbox_push(&mailbox, &message) == 0);
    message = make_message(2);
    message.size = sizeof(OsIpcMessage) - 1u;
    check(ipc_mailbox_push(&mailbox, &message) == 0);
    check(ipc_mailbox_push(0, &message) == 0);
    check(ipc_mailbox_push(&mailbox, 0) == 0);
    check(ipc_mailbox_pop(0, &message) == 0);
    check(ipc_mailbox_pop(&mailbox, 0) == 0);

    for (uint32_t i = 0; i < 3; i++) {
        message = make_message(200u + i);
        check(ipc_mailbox_push(&mailbox, &message) == 1);
    }
    check(ipc_mailbox_count(&mailbox) == 3);
    ipc_mailbox_drop_all(&mailbox);
    check(ipc_mailbox_count(&mailbox) == 0);
    check(ipc_mailbox_dropped_count(&mailbox) == 4);
    check(ipc_mailbox_delivered_count(&mailbox) == 21);
    KernelIpcMailboxStats mailbox_stats;
    ipc_mailbox_get_stats(&mailbox, &mailbox_stats);
    check(mailbox_stats.capacity == IPC_MAILBOX_CAPACITY);
    check(mailbox_stats.count <= mailbox_stats.capacity);
    check(mailbox_stats.count == 0);
    check(mailbox_stats.delivered_count == 21);
    check(mailbox_stats.dropped_count == 4);

    Process process;
    process_clear(&process);
    check(process_ipc_mailbox_count(&process) == 0);
    check(process_ipc_mailbox_delivered_count(&process) == 0);
    check(process_ipc_mailbox_dropped_count(&process) == 0);

    message = make_message(300);
    check(process_ipc_mailbox_push(&process, &message) == 1);
    check(process_ipc_mailbox_count(&process) == 1);
    check(process_ipc_mailbox_pop(&process, &message) == 1);
    expect_message(&message, 300);
    check(process_ipc_mailbox_delivered_count(&process) == 1);

    for (uint32_t i = 0; i < 3; i++) {
        message = make_message(400u + i);
        check(process_ipc_mailbox_push(&process, &message) == 1);
    }
    process.pid = 42;
    process.parent_pid = 7;
    process.active = 1;
    process_mark_returned(&process, PROCESS_TERM_EXIT, 0);
    check(process.state == PROCESS_STATE_RETURNED);
    check(process.active == 0);
    check(process_ipc_mailbox_count(&process) == 0);
    check(process_ipc_mailbox_delivered_count(&process) == 0);
    check(process_ipc_mailbox_dropped_count(&process) == 0);

    message = make_message(500);
    check(process_ipc_mailbox_push(&process, &message) == 1);
    process.active = 1;
    process_mark_failed(&process, PROCESS_TERM_KILLED, 9);
    check(process.state == PROCESS_STATE_FAILED);
    check(process.status_code == 9);
    check(process_ipc_mailbox_count(&process) == 0);

    message = make_message(600);
    check(process_ipc_mailbox_push(&process, &message) == 1);
    process_clear(&process);
    check(process.pid == 0);
    check(process_ipc_mailbox_count(&process) == 0);
    check(process_ipc_mailbox_delivered_count(&process) == 0);
    check(process_ipc_mailbox_dropped_count(&process) == 0);

    check(process_ipc_mailbox_push(0, &message) == 0);
    check(process_ipc_mailbox_pop(0, &message) == 0);
    check(process_ipc_mailbox_count(0) == 0);
    check(process_ipc_mailbox_delivered_count(0) == 0);
    check(process_ipc_mailbox_dropped_count(0) == 0);

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
    with tempfile.TemporaryDirectory(prefix="os64_ipc_mailbox_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "ipc_mailbox_test.cpp"
        stub_path = temp_path / "ipc_mailbox_stubs.cpp"
        binary_path = temp_path / "ipc_mailbox_test"
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
            str(REPO_ROOT / "kernel/debug/fault_injection.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_objects.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
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

    print("ipc mailbox test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
