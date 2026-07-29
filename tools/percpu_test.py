#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


HARNESS = r'''
#include <stdint.h>
#include "kernel/acpi.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"

static int failures = 0;
#define CHECK(v) do { if (!(v)) failures++; } while (0)

int main() {
    AcpiState acpi = {};
    acpi.cpu_parse_valid = 1;
    acpi.cpu_count = 4;
    for (uint32_t i = 0; i < 4; i++) {
        acpi.cpus[i].acpi_id = i;
        acpi.cpus[i].apic_id = i;
        acpi.cpus[i].flags = 1;
        acpi.cpus[i].enabled = 1;
        acpi.cpus[i].xapic_supported = 1;
    }
    CHECK(cpu_topology_init(&acpi, 0));
    CHECK(cpu_local_system_init());
    CHECK(cpu_local_current() == cpu_local_by_id(0));
    CHECK(cpu_local_by_id(0)->online == 1);
    for (uint32_t i = 1; i < 4; i++) {
        CHECK(cpu_record(i)->lifecycle == CPU_STATE_PREPARED);
        CHECK(cpu_local_by_id(i)->prepared == 1);
        CHECK(cpu_local_by_id(i)->online == 0);
    }

    cpu_local_by_id(0)->entry_depth = 7;
    cpu_local_by_id(1)->entry_depth = 2;
    CHECK(cpu_local_by_id(0)->entry_depth == 7);
    CHECK(cpu_local_by_id(1)->entry_depth == 2);

    for (uint32_t i = 0; i < 4; i++) {
        CpuLocal* local = cpu_local_by_id(i);
        CHECK(cpu_local_validate(local));
        CHECK(cpu_local_resolve_emergency(local->nmi_stack_top - 64,
                                          CPU_EMERGENCY_NMI) == local);
        CHECK(cpu_local_resolve_emergency(local->double_fault_stack_top - 64,
                                          CPU_EMERGENCY_DOUBLE_FAULT) == local);
        CHECK(cpu_local_resolve_emergency(local->kernel_stack_top - 64,
                                          CPU_EMERGENCY_NMI) == 0);
    }

    CpuLocal* ap = cpu_local_by_id(2);
    uint64_t saved_magic = ap->magic;
    ap->magic = 0;
    CHECK(!cpu_local_validate(ap));
    CHECK(cpu_local_resolve_emergency(ap->nmi_stack_top - 64,
                                      CPU_EMERGENCY_NMI) == 0);
    ap->magic = saved_magic;

    CpuLocal* bsp = cpu_local_by_id(0);
    CpuLocal* entered = cpu_local_emergency_enter(bsp->nmi_stack_top - 64,
                                                  CPU_EMERGENCY_NMI);
    CHECK(entered == bsp && bsp->nmi_count == 1 && bsp->emergency_active);
    CHECK(cpu_local_emergency_enter(bsp->nmi_stack_top - 64,
                                    CPU_EMERGENCY_NMI) == 0);
    CHECK(bsp->emergency_failure_count == 1);
    cpu_local_emergency_leave(bsp, CPU_EMERGENCY_NMI);
    CHECK(!bsp->emergency_active && bsp->interrupt_depth == 0);
    return failures == 0 ? 0 : 1;
}
'''


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    cpu_local_source = (root / "kernel/cpu/cpu_local.cpp").read_text(encoding="utf-8")
    if "swapgs" in cpu_local_source.lower():
        raise SystemExit("CPU-local source unexpectedly uses SWAPGS")
    if "cr4 &= ~(1ULL << 16)" not in cpu_local_source:
        raise SystemExit("CPU-local source does not keep FSGSBASE disabled")
    with tempfile.TemporaryDirectory(prefix="os64-percpu-") as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-DOS64_HOST_TEST", f"-I{root / 'include'}",
            str(source),
            str(root / "kernel/cpu/cpu.cpp"),
            str(root / "kernel/cpu/cpu_local.cpp"),
            str(root / "kernel/debug/fault_injection.cpp"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("per-CPU state and emergency stack contracts OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
