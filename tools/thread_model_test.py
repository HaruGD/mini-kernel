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
#include "kernel/syscall64.h"

static int failures = 0;
static void check(int value) { if (!value) failures++; }

int main() {
    service_registry_init();
    process_system_init();

    Process* process = allocate_process_record();
    check(process != 0);
    process_assign_identity(process, next_pid++, 0);
    process->active = 1;
    process->state = PROCESS_STATE_RUNNING;

    Thread* main_thread = process_main_thread(process);
    check(main_thread != 0);
    check(main_thread->is_main != 0);
    check(main_thread->owner == process);
    check(main_thread->context == &process->main_thread_context);
    check(process->thread_count == 1);
    check(thread_identity_matches(main_thread, process->main_thread_identity));
    check(main_thread->context->kernel_stack_base != 0);

    process->runtime_ticks = 17;
    check(main_thread->context->runtime_ticks == 17);
    main_thread->context->timeslice_ticks = 3;
    check(process->timeslice_ticks == 3);

    Thread* extra[3];
    for (uint32_t i = 0; i < 3; i++) {
        extra[i] = allocate_thread_record(process, 0);
        check(extra[i] != 0);
        check(extra[i]->owner == process);
        check(extra[i]->context != &process->main_thread_context);
        check(extra[i]->context->kernel_stack_base !=
              main_thread->context->kernel_stack_base);
    }
    check(process->thread_count == THREADS_PER_PROCESS_MAX);
    check(allocate_thread_record(process, 0) == 0);

    uint32_t join_status = 0;
    check(thread_join_begin(main_thread,
                            thread_identity(main_thread),
                            0,
                            0,
                            0,
                            &join_status) == SYS_ERR_INVALID_ARGUMENT);
    Process* other_process = allocate_process_record();
    check(other_process != 0);
    process_assign_identity(other_process, next_pid++, 0);
    other_process->active = 1;
    other_process->state = PROCESS_STATE_RUNNING;
    Thread* other_main = process_main_thread(other_process);
    check(other_main != 0);
    check(thread_join_begin(main_thread,
                            thread_identity(other_main),
                            0,
                            0,
                            0,
                            &join_status) == SYS_ERR_NOT_FOUND);
    process_clear(other_process);

    ThreadIdentity old_identity = thread_identity(extra[0]);
    extra[0]->context->resumable = 1;
    scheduler_enqueue_thread(extra[0]);
    check(sched_queue_count == 1);
    check(sched_queue[0] == extra[0]);
    scheduler_enqueue_thread(extra[0]);
    check(sched_queue_count == 1);
    scheduler_remove_thread(extra[0]);
    check(sched_queue_count == 0);

    process_clear(process);
    check(find_thread_by_identity(old_identity) == 0);
    process_assign_identity(process, next_pid++, 0);
    process->active = 1;
    process->state = PROCESS_STATE_RUNNING;
    Thread* replacement = process_main_thread(process);
    check(replacement != 0);
    check(replacement->generation != old_identity.generation ||
          replacement->tid != old_identity.tid);

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
    with tempfile.TemporaryDirectory(prefix="os64_thread_model_") as temporary:
        temp = Path(temporary)
        source = temp / "thread_model.cpp"
        stubs = temp / "stubs.cpp"
        binary = temp / "thread_model"
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
    print("thread object model test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
