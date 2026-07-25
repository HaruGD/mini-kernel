#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <stdint.h>
#include <thread>
#include <vector>
#include "kernel/spinlock.h"

static int failures = 0;
static void check(int condition) { if (!condition) failures++; }

int main() {
    KernelSpinlock process_lock;
    KernelSpinlock handle_lock;
    kernel_spinlock_init(&process_lock, KERNEL_LOCK_CLASS_PROCESS, "process");
    kernel_spinlock_init(&handle_lock, KERNEL_LOCK_CLASS_HANDLE, "handle");
    kernel_spinlock_reset_stats();
    kernel_host_set_interrupts_enabled(1);

    KernelSpinlockToken process_token;
    KernelSpinlockToken handle_token;
    check(kernel_spinlock_acquire(&process_lock, &process_token) == 1);
    check(kernel_interrupts_enabled() == 0);
    check(kernel_spinlock_acquire(&handle_lock, &handle_token) == 1);
    kernel_spinlock_release(&handle_lock, &handle_token);
    check(kernel_interrupts_enabled() == 0);
    kernel_spinlock_release(&process_lock, &process_token);
    check(kernel_interrupts_enabled() == 1);

    KernelSpinlockToken high;
    KernelSpinlockToken inverted;
    check(kernel_spinlock_acquire(&handle_lock, &high) == 1);
    check(kernel_spinlock_acquire(&process_lock, &inverted) == 0);
    kernel_spinlock_release(&handle_lock, &high);

    KernelSpinlockToken outer;
    KernelSpinlockToken recursive;
    check(kernel_spinlock_acquire(&process_lock, &outer) == 1);
    check(kernel_spinlock_acquire(&process_lock, &recursive) == 0);
    kernel_spinlock_release(&process_lock, &outer);

    KernelSpinlock wrong_cpu_lock;
    kernel_spinlock_init(&wrong_cpu_lock, KERNEL_LOCK_CLASS_PROCESS, "wrong_cpu");
    KernelSpinlockToken owner_token;
    check(kernel_spinlock_acquire(&wrong_cpu_lock, &owner_token) == 1);
    std::thread wrong_releaser([&]() {
        kernel_host_set_interrupts_enabled(1);
        kernel_spinlock_release(&wrong_cpu_lock, &owner_token);
    });
    wrong_releaser.join();
    check(wrong_cpu_lock.locked == 1);
    kernel_spinlock_release(&wrong_cpu_lock, &owner_token);

    KernelSpinlock contention_lock;
    kernel_spinlock_init(&contention_lock, KERNEL_LOCK_CLASS_PROCESS, "contention");
    uint32_t counter = 0;
    std::vector<std::thread> workers;
    for (uint32_t worker = 0; worker < 4; worker++) {
        workers.emplace_back([&]() {
            kernel_host_set_interrupts_enabled(1);
            for (uint32_t i = 0; i < 10000; i++) {
                KernelSpinlockToken token;
                if (kernel_spinlock_acquire(&contention_lock, &token)) {
                    counter++;
                    kernel_spinlock_release(&contention_lock, &token);
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(counter == 40000);

    KernelSpinlockStats stats;
    kernel_spinlock_get_stats(&stats);
    check(stats.acquisitions >= 40004);
    check(stats.order_violations == 1);
    check(stats.recursion_violations == 1);
    check(stats.release_violations == 1);
    check(stats.wrong_cpu_violations == 1);
    check(stats.schedule_violations == 0);
    check(stats.preemption_disable_depth == 0);
    check(stats.maximum_depth >= 2);
    check(stats.current_depth == 0);
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_spinlock_") as temp:
        source = Path(temp) / "spinlock_test.cpp"
        binary = Path(temp) / "spinlock_test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/sync/spinlock.cpp"), str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("spinlock test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
