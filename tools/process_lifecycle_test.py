#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/fault_injection.h"
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static void clear_all() {
    service_registry_init();
    next_pid = 1;
    next_process_generation = 1;
    next_tid = 1;
    next_thread_generation = 1;
    process_execution_reset();
    sched_queue_count = 0;
    sched_queue_head = 0;
    sched_last_pid = 0;
    sched_switch_count = 0;
    sched_yield_count = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
    for (uint32_t i = 0; i < SCHED_QUEUE_SIZE; i++) {
        sched_queue[i] = 0;
    }
    process_clear_focus(0);
}

static void assign(Process* process, const Process* parent) {
    process_assign_identity(process, next_pid++, parent);
    process->active = 1;
    process->state = PROCESS_STATE_RUNNING;
}

int main() {
    clear_all();

    kernel_fault_injection_reset();
    kernel_fault_injection_arm(KERNEL_FAULT_POINT_PROCESS, 0);
    check(allocate_process_record() == 0);
    check(process_table[0].pid == 0);

    Process* parent = &process_table[0];
    assign(parent, 0);
    ProcessIdentity parent_identity = process_identity(parent);
    check(parent_identity.pid == 1);
    check(parent_identity.generation != 0);
    check(find_process_by_identity(parent_identity) == parent);

    Process* child = &process_table[1];
    assign(child, parent);
    check(child->parent_pid == parent->pid);
    check(child->parent_generation == parent->generation);

    parent->state = PROCESS_STATE_PAUSED;
    parent->resumable = 1;
    check(process_wait_begin(parent, PROCESS_WAIT_CHILD, 0, 0, 0) == 1);
    check(process_set_focus(child->pid) == 1);
    process_mark_returned(child, PROCESS_TERM_EXIT, 7);
    check(process_focused_pid() == parent->pid);
    check(parent->wait_pending == 0);
    check(parent->wait_result == PROCESS_WAIT_OK);
    check(parent->scheduler_state == SCHED_STATE_READY);
    check(find_waitable_child_process(parent->pid) == child);
    check(reap_all_child_processes(parent->pid) == 1);
    check(child->reaped == 1);

    ProcessIdentity stale_child = process_identity(child);
    process_clear(child);
    process_assign_identity(child, stale_child.pid, parent);
    check(child->generation != stale_child.generation);
    check(find_process_by_identity(stale_child) == 0);
    check(find_process_by_identity_compat(child->pid, child->generation) == child);
    check(find_process_by_identity_compat(child->pid, stale_child.generation) == 0);
    process_mark_failed(child, PROCESS_TERM_KILLED, 9);
    child->reaped = 1;

    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
    assign(parent, 0);
    Process* active_child = &process_table[1];
    assign(active_child, parent);
    Process* terminal_child = &process_table[2];
    assign(terminal_child, parent);
    process_mark_returned(terminal_child, PROCESS_TERM_EXIT, 0);
    check(terminal_child->reaped == 0);
    process_mark_returned(parent, PROCESS_TERM_EXIT, 0);
    check(active_child->parent_pid == 0);
    check(active_child->parent_generation == 0);
    check(terminal_child->reaped == 1);

    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
    assign(parent, 0);
    for (uint32_t i = 0; i < 5; i++) {
        Process* result = &process_table[i + 1];
        assign(result, parent);
        process_mark_returned(result, PROCESS_TERM_EXIT, i);
    }
    uint32_t unreaped = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid != 0 && process->parent_pid == parent->pid &&
            !process->active && !process->reaped) {
            unreaped++;
        }
    }
    check(unreaped == PROCESS_CHILD_RESULT_HISTORY_LIMIT);

    clear_all();
    assign(parent, 0);
    for (uint32_t i = 0; i < 1000; i++) {
        Process* slot = allocate_process_record();
        check(slot != 0);
        if (slot == 0) {
            break;
        }
        assign(slot, parent);
        process_mark_returned(slot, PROCESS_TERM_EXIT, i);
        slot->reaped = 1;
    }

    uint32_t reusable = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || (!process->active && process->reaped)) {
            reusable++;
        }
    }
    check(reusable >= PROCESS_TABLE_SIZE - 1);

    SchedulerDiagnosticSnapshot snapshot;
    process_get_diagnostic_snapshot(&snapshot);
    check(snapshot.process_count == PROCESS_TABLE_SIZE);
    check(snapshot.queue_count <= SCHED_QUEUE_SIZE);
    for (uint32_t i = 0; i < snapshot.process_count; i++) {
        const ProcessDiagnosticSnapshot* item = &snapshot.processes[i];
        check(item->mailbox.count <= item->mailbox.capacity);
        check(item->handle_count <= KERNEL_HANDLE_TABLE_SIZE);
        if (item->state == PROCESS_STATE_RETURNED || item->state == PROCESS_STATE_FAILED) {
            check(item->active == 0);
            check(item->scheduler_state == SCHED_STATE_FINISHED);
        }
    }

    clear_all();
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
    with tempfile.TemporaryDirectory(prefix="os64_process_lifecycle_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "process_lifecycle_test.cpp"
        stub_path = temp_path / "process_lifecycle_stubs.cpp"
        binary_path = temp_path / "process_lifecycle_test"
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
            "-I",
            str(REPO_ROOT / "tools/fixtures"),
            str(REPO_ROOT / "kernel/sync/spinlock.cpp"),
            str(REPO_ROOT / "kernel/debug/fault_injection.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_objects.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(REPO_ROOT / "kernel/graphics/surface_backing.cpp"),
            str(REPO_ROOT / "tools/fixtures/kernel_mm_host_stubs.cpp"),
            str(REPO_ROOT / "kernel/ipc/ipc_mailbox.cpp"),
            str(REPO_ROOT / "kernel/input/input_event_queue.cpp"),
            str(REPO_ROOT / "kernel/process/process64.cpp"),
            str(REPO_ROOT / "kernel/process/process_surface.cpp"),
            str(REPO_ROOT / "kernel/mm/address_space.cpp"),
            str(REPO_ROOT / "kernel/service/service_registry.cpp"),
            str(source_path),
            str(stub_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("process lifecycle test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
