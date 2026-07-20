#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


SOURCE = r"""
#include <stdint.h>
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"

static int failures = 0;
static void check(int value) { if (!value) failures++; }

int main() {
    service_registry_init();
    process_system_init();

    Process* process = allocate_process_record();
    process_assign_identity(process, next_pid++, 0);
    process->active = 1;
    process->state = PROCESS_STATE_RUNNING;
    Thread* threads[3];
    threads[0] = process_main_thread(process);
    threads[1] = allocate_thread_record(process, 0);
    threads[2] = allocate_thread_record(process, 0);
    for (uint32_t i = 0; i < 3; i++) {
        check(threads[i] != 0);
        threads[i]->context->resumable = 1;
        threads[i]->context->scheduler_state = SCHED_STATE_RUNNING;
    }

    check(thread_wait_begin(threads[0], PROCESS_WAIT_IPC, 0x1000, 0, 10) == 1);
    check(thread_wait_begin(threads[1], PROCESS_WAIT_IPC, 0x2000, 0, 10) == 1);
    check(thread_wait_begin(threads[2], PROCESS_WAIT_IPC, 0x3000, 0, 10) == 1);
    check(thread_wait_begin(threads[2], PROCESS_WAIT_INPUT, 0, 0, 0) == 0);
    check(process_wait_count(process, PROCESS_WAIT_IPC) == 3);
    check(process_wait_signal(process, PROCESS_WAIT_IPC, PROCESS_WAIT_OK) == 1);
    check(!thread_wait_is_pending(threads[0]));
    check(thread_wait_is_pending(threads[1]));
    check(thread_wait_is_pending(threads[2]));
    check(process_wait_count(process, PROCESS_WAIT_IPC) == 2);
    check(process_wait_signal(process, PROCESS_WAIT_IPC, PROCESS_WAIT_OK) == 1);
    check(!thread_wait_is_pending(threads[1]));
    check(thread_wait_is_pending(threads[2]));
    check(process_wait_cancel(process, PROCESS_WAIT_IPC,
                              PROCESS_WAIT_CANCELLED) == 1);
    check(threads[2]->context->wait_result == PROCESS_WAIT_CANCELLED);
    check(process_wait_signal(process, PROCESS_WAIT_IPC, PROCESS_WAIT_OK) == 0);

    for (uint32_t i = 0; i < 3; i++) {
        thread_wait_reset(threads[i]);
        threads[i]->context->resumable = 1;
    }
    check(thread_wait_begin(threads[0], PROCESS_WAIT_INPUT, 0, 0, 0) == 1);
    check(thread_wait_begin(threads[1], PROCESS_WAIT_INPUT, 0, 0, 0) == 1);
    check(thread_wait_begin(threads[2], PROCESS_WAIT_INPUT, 0, 0, 0) == 1);
    check(process_wait_cancel(process, PROCESS_WAIT_INPUT,
                              PROCESS_WAIT_CANCELLED) == 3);
    for (uint32_t i = 0; i < 3; i++) {
        check(!thread_wait_is_pending(threads[i]));
        check(threads[i]->context->wait_result == PROCESS_WAIT_CANCELLED);
        check(threads[i]->context->scheduler_state == SCHED_STATE_READY);
        thread_wait_reset(threads[i]);
        threads[i]->context->resumable = 1;
    }

    check(thread_wait_begin(threads[0], PROCESS_WAIT_TIMER, 0, 5, 100) == 1);
    check(thread_wait_begin(threads[1], PROCESS_WAIT_TIMER, 0, 7, 100) == 1);
    process_wait_tick(104);
    check(thread_wait_is_pending(threads[0]));
    check(thread_wait_is_pending(threads[1]));
    process_wait_tick(105);
    check(!thread_wait_is_pending(threads[0]));
    check(threads[0]->context->wait_result == PROCESS_WAIT_OK);
    check(thread_wait_is_pending(threads[1]));
    process_wait_tick(107);
    check(!thread_wait_is_pending(threads[1]));
    check(threads[1]->context->wait_result == PROCESS_WAIT_OK);

    thread_wait_reset(threads[0]);
    threads[0]->context->resumable = 1;
    check(thread_wait_begin(threads[0], PROCESS_WAIT_TIMER, 0, 3,
                            0xFFFFFFFEu) == 1);
    process_wait_tick(0);
    check(thread_wait_is_pending(threads[0]));
    process_wait_tick(1);
    check(!thread_wait_is_pending(threads[0]));

    process_clear(process);
    return failures == 0 ? 0 : 1;
}
"""


STUBS = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
uint32_t vfs_close_all_for_owner(uint32_t) { return 0; }
extern "C" void* kmalloc(size_t size) { return malloc(size); }
extern "C" void kfree(void* pointer) { free(pointer); }
void copy_string64(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) return;
    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_thread_wait_") as temporary:
        temp = Path(temporary)
        source = temp / "thread_wait.cpp"
        stubs = temp / "stubs.cpp"
        binary = temp / "thread_wait"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        stubs.write_text(textwrap.dedent(STUBS), encoding="utf-8")
        command = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            "-I", str(ROOT / "tools/fixtures"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/debug/fault_injection.cpp"),
            str(ROOT / "kernel/handle/kernel_handle.cpp"),
            str(ROOT / "kernel/handle/kernel_objects.cpp"),
            str(ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(ROOT / "kernel/graphics/surface_backing.cpp"),
            str(ROOT / "tools/fixtures/kernel_mm_host_stubs.cpp"),
            str(ROOT / "kernel/ipc/ipc_mailbox.cpp"),
            str(ROOT / "kernel/input/input_event_queue.cpp"),
            str(ROOT / "kernel/process/process64.cpp"),
            str(ROOT / "kernel/process/process_surface.cpp"),
            str(ROOT / "kernel/mm/address_space.cpp"),
            str(ROOT / "kernel/service/service_registry.cpp"),
            str(source), str(stubs), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print("thread wait/wake test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
