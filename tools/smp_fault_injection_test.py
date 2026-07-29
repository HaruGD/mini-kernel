#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "kernel/fault_injection.h"

static int failures = 0;
static void check(bool condition) {
    if (!condition) {
        failures++;
    }
}

int main() {
    const uint32_t points[] = {
        KERNEL_FAULT_POINT_CPU_LOCAL,
        KERNEL_FAULT_POINT_AP_STARTUP,
        KERNEL_FAULT_POINT_LOCAL_TIMER,
        KERNEL_FAULT_POINT_SCHEDULER_CLAIM,
        KERNEL_FAULT_POINT_TLB_REQUEST,
        KERNEL_FAULT_POINT_IRQ_OWNER,
    };

    for (uint32_t point : points) {
        kernel_fault_injection_reset();
        check(kernel_fault_injection_arm(point, 31));
        std::atomic<uint32_t> observed_failures{0};
        std::vector<std::thread> workers;
        for (uint32_t worker = 0; worker < 8; worker++) {
            workers.emplace_back([point, &observed_failures]() {
                for (uint32_t attempt = 0; attempt < 64; attempt++) {
                    if (kernel_fault_injection_should_fail(point)) {
                        observed_failures.fetch_add(1,
                                                    std::memory_order_relaxed);
                    }
                }
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }

        KernelFaultInjectionSnapshot snapshot = {};
        kernel_fault_injection_get_snapshot(&snapshot);
        check(observed_failures.load(std::memory_order_relaxed) == 1);
        check(snapshot.points[point].attempts == 512);
        check(snapshot.points[point].failures == 1);
        check(snapshot.points[point].remaining == 0);
        check(snapshot.points[point].armed == 0);
        check(kernel_fault_point_from_name(kernel_fault_point_name(point)) ==
              point);
    }
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_smp_faults_") as directory:
        source = Path(directory) / "smp_faults.cpp"
        binary = Path(directory) / "smp_faults"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I", str(ROOT / "include"),
            str(ROOT / "kernel/debug/fault_injection.cpp"),
            str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("SMP fault injection one-shot contention test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
