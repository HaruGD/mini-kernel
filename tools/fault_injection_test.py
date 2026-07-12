#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <stdint.h>
#include "kernel/fault_injection.h"

static int failures = 0;
static void check(int condition) { if (!condition) failures++; }

int main() {
    kernel_fault_injection_reset();
    for (uint32_t point = 0; point < KERNEL_FAULT_POINT_COUNT; point++) {
        check(kernel_fault_point_from_name(kernel_fault_point_name(point)) == point);
        check(kernel_fault_injection_arm(point, 2) == 1);
        check(kernel_fault_injection_should_fail(point) == 0);
        check(kernel_fault_injection_should_fail(point) == 0);
        check(kernel_fault_injection_should_fail(point) == 1);
        check(kernel_fault_injection_should_fail(point) == 0);
    }
    check(kernel_fault_point_from_name("unknown") == KERNEL_FAULT_POINT_INVALID);
    check(kernel_fault_injection_arm(KERNEL_FAULT_POINT_INVALID, 0) == 0);
    check(kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_INVALID) == 0);

    KernelFaultInjectionSnapshot snapshot;
    kernel_fault_injection_get_snapshot(&snapshot);
    for (uint32_t point = 0; point < KERNEL_FAULT_POINT_COUNT; point++) {
        check(snapshot.points[point].attempts == 4);
        check(snapshot.points[point].failures == 1);
        check(snapshot.points[point].armed == 0);
    }
    kernel_fault_injection_reset();
    kernel_fault_injection_get_snapshot(&snapshot);
    for (uint32_t point = 0; point < KERNEL_FAULT_POINT_COUNT; point++) {
        check(snapshot.points[point].attempts == 0);
        check(snapshot.points[point].failures == 0);
    }
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_fault_injection_") as temp:
        source = Path(temp) / "fault_injection_test.cpp"
        binary = Path(temp) / "fault_injection_test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            str(ROOT / "kernel/debug/fault_injection.cpp"),
            str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("fault injection test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
