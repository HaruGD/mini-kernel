#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <stdint.h>
#include <stdio.h>

#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall/dispatcher.h"
#include "kernel/syscall/user_memory.h"
#include "kernel/syscall64.h"
#include "kernel/thread.h"
#include "os64/process_types.h"
#include "os64/syscall_numbers.h"

static Process process;
static Thread thread;
static Process other_process;
static Process* current_process_value = &process;
static Thread* current_thread_value = &thread;
static uint32_t lease_calls = 0;
static int lease_result = OS_SUCCESS;
static uint64_t handler_result = 0x1234u;
static uint64_t handler_calls = 0;
static int failures = 0;

static void check_at(int condition, int line) {
    if (!condition) {
        failures++;
        fprintf(stderr, "dispatcher check failed at line %d\n", line);
    }
}
#define check(condition) check_at((condition), __LINE__)

Process* current_process() { return current_process_value; }
Thread* current_thread() { return current_thread_value; }
int process_has_permissions(const Process* target, uint32_t permissions) {
    return target != 0 &&
           (permissions & ~OS_PROCESS_PERMISSION_VALID_MASK) == 0 &&
           (target->permissions & permissions) == permissions;
}
int user_address_is_canonical(uint64_t address) {
    return address < 0x0000800000000000ULL;
}
OsResult user_checked_add_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if (output == 0) return OS_ERR_INVALID_ARGUMENT;
    *output = 0;
    if (left > UINT64_MAX - right) return OS_ERR_OVERFLOW;
    *output = left + right;
    return OS_SUCCESS;
}
OsResult user_memory_lease_begin(Process*, uint64_t address, uint64_t size,
                                 uint32_t, uint32_t, uint32_t,
                                 UserMemoryLease* lease) {
    lease_calls++;
    if (lease_result != OS_SUCCESS) return lease_result;
    if (address != 0x2000u || size != 1) return OS_ERR_BAD_BUFFER;
    *lease = {};
    lease->active = 1;
    return OS_SUCCESS;
}
void user_memory_lease_end(UserMemoryLease* lease) {
    if (lease != 0) lease->active = 0;
}
uint64_t syscall_dispatch_handler64(uint64_t, uint64_t, uint64_t, uint64_t) {
    handler_calls++;
    return handler_result;
}

static void reset_context() {
    process = {};
    thread = {};
    other_process = {};
    process.pid = 7;
    process.generation = 9;
    process.active = 1;
    process.state = PROCESS_STATE_RUNNING;
    thread.tid = 11;
    thread.generation = 13;
    thread.owner_pid = process.pid;
    thread.owner_generation = process.generation;
    thread.owner = &process;
    thread.active = 1;
    current_process_value = &process;
    current_thread_value = &thread;
    lease_calls = 0;
    lease_result = OS_SUCCESS;
    handler_result = 0x1234u;
    handler_calls = 0;
    syscall_dispatch_reset_diagnostics();
}

int main() {
    reset_context();
    check(syscall_descriptor_lookup(1) != 0);
    check(syscall_descriptor_lookup(OS64_SYSCALL_MAX_NUMBER) != 0);
    check(syscall_descriptor_lookup(0) == 0);
    check(syscall_descriptor_lookup(OS64_SYSCALL_MAX_NUMBER + 1u) == 0);

    check(syscall_dispatch64(OS_SYS_GETPID, 0, 0, 0) == handler_result);
    check(handler_calls == 1 && lease_calls == 0);
    check((int64_t)syscall_dispatch64(OS64_SYSCALL_MAX_NUMBER + 1u,
                                      0, 0, 0) == OS_ERR_UNSUPPORTED);

    current_process_value = 0;
    check((int64_t)syscall_dispatch64(OS_SYS_GETPID, 0, 0, 0) ==
          OS_ERR_NOT_READY);
    current_process_value = &process;
    process.state = PROCESS_STATE_RETURNED;
    check((int64_t)syscall_dispatch64(OS_SYS_GETPID, 0, 0, 0) ==
          OS_ERR_NOT_READY);
    process.state = PROCESS_STATE_RUNNING;
    current_thread_value = 0;
    check((int64_t)syscall_dispatch64(OS_SYS_GETPID, 0, 0, 0) ==
          OS_ERR_NOT_READY);
    current_thread_value = &thread;
    thread.owner = &other_process;
    check((int64_t)syscall_dispatch64(OS_SYS_GETPID, 0, 0, 0) ==
          OS_ERR_NOT_READY);
    thread.owner = &process;

    process.permissions = 0x80000000u;
    check((int64_t)syscall_dispatch64(OS_SYS_GETPID, 0, 0, 0) ==
          OS_ERR_PERMISSION_DENIED);
    process.permissions = 0;
    lease_calls = 0;
    check((int64_t)syscall_dispatch64(OS_SYS_IPC_V2_RECV,
                                      0xFFFF800000000000ULL, 0, 0) ==
          OS_ERR_PERMISSION_DENIED);
    check(lease_calls == 0);

    process.permissions = OS_PROCESS_PERMISSION_IPC;
    check((int64_t)syscall_dispatch64(OS_SYS_IPC_V2_RECV, 0, 0, 0) ==
          OS_ERR_BAD_BUFFER);
    check((int64_t)syscall_dispatch64(OS_SYS_IPC_V2_RECV,
                                      0xFFFF800000000000ULL, 0, 0) ==
          OS_ERR_BAD_BUFFER);
    check((int64_t)syscall_dispatch64(OS_SYS_IPC_V2_RECV,
                                      0x3000u, 0, 0) == OS_ERR_BAD_BUFFER);
    lease_result = OS_ERR_NOT_READY;
    check((int64_t)syscall_dispatch64(OS_SYS_IPC_V2_RECV,
                                      0x2000u, 0, 0) == OS_ERR_NOT_READY);
    lease_result = OS_SUCCESS;
    check(syscall_dispatch64(OS_SYS_IPC_V2_RECV, 0x2000u, 0, 0) ==
          handler_result);

    lease_calls = 0;
    check(syscall_dispatch64(OS_SYS_IPC_QUERY, 0, 0, 0) == handler_result);
    check(lease_calls == 0);
    check(syscall_dispatch64(OS_SYS_IPC_QUERY,
                             0xFFFF800000000000ULL, 0, 0) == handler_result);
    check(lease_calls == 0);

    check((int64_t)syscall_dispatch64(OS_SYS_WRITE,
                                      UINT64_MAX - 3u, 8u, 0) ==
          OS_ERR_OVERFLOW);
    check(lease_calls == 0);

    process.permissions = 0;
    check((int64_t)syscall_dispatch64(OS_SYS_GFX_CLEAR, 0, 0, 0) ==
          OS_ERR_PERMISSION_DENIED);
    process.permissions = OS_PROCESS_PERMISSION_DISPLAY;
    check(syscall_dispatch64(OS_SYS_GFX_CLEAR, 0, 0, 0) == handler_result);

    SyscallDispatchDiagnostics diagnostics;
    syscall_dispatch_get_diagnostics(&diagnostics);
    check(diagnostics.total_calls == 18);
    check(diagnostics.dispatched_calls == 5);
    check(diagnostics.rejected_calls == 13);
    uint64_t rejection_sum = 0;
    for (uint32_t reason = 1; reason < SYSCALL_REJECT_REASON_COUNT; reason++) {
        rejection_sum += diagnostics.rejected_by_reason[reason];
        check(syscall_reject_reason_name(reason)[0] != '\0');
    }
    check(rejection_sum == diagnostics.rejected_calls);
    check(diagnostics.rejected_by_reason[SYSCALL_REJECT_PERMISSION_DENIED] == 2);
    check(diagnostics.rejected_by_reason[SYSCALL_REJECT_UNKNOWN_NUMBER] == 1);
    check(diagnostics.rejected_by_reason[SYSCALL_REJECT_NULL_POINTER] == 1);
    check(diagnostics.rejected_by_reason[SYSCALL_REJECT_POINTER_FORMAT] == 2);
    check(diagnostics.rejected_by_reason[SYSCALL_REJECT_POINTER_ACCESS] == 1);
    check(diagnostics.rejected_by_reason[SYSCALL_REJECT_EXECUTION_CONTEXT] == 1);
    check(kernel_syscall_count() == diagnostics.total_calls);
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_syscall_dispatch_") as directory:
        temp = Path(directory)
        source = temp / "syscall_dispatch_test.cpp"
        binary = temp / "syscall_dispatch_test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/syscall/dispatcher.cpp"),
            str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("syscall descriptor dispatch test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
