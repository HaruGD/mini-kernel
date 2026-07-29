#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "kernel/acpi.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"
#include "kernel/fault_injection.h"
#include "kernel/mm/address_space.h"
#include "kernel/smp.h"

static int failures = 0;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value); failures++; } } while (0)

int main() {
    AcpiState acpi = {};
    acpi.cpu_parse_valid = 1;
    acpi.cpu_count = 2;
    for (uint32_t i = 0; i < 2; i++) {
        acpi.cpus[i].acpi_id = i;
        acpi.cpus[i].apic_id = i;
        acpi.cpus[i].flags = 1;
        acpi.cpus[i].enabled = 1;
        acpi.cpus[i].xapic_supported = 1;
    }

    CHECK(cpu_topology_init(&acpi, 0));
    kernel_fault_injection_reset();
    CHECK(kernel_fault_injection_arm(KERNEL_FAULT_POINT_CPU_LOCAL, 0));
    CHECK(!cpu_local_system_init());
    KernelFaultInjectionSnapshot faults = {};
    kernel_fault_injection_get_snapshot(&faults);
    CHECK(faults.points[KERNEL_FAULT_POINT_CPU_LOCAL].failures == 1);
    kernel_fault_injection_reset();
    CHECK(cpu_local_system_init());
    CHECK(smp_start_application_processors());
    CpuLocal* ap = cpu_local_by_id(1);
    const uint64_t stack_top = ap->kernel_stack_top;
    CHECK(cpu_record(1)->lifecycle == CPU_STATE_PREPARED);
    CHECK(kernel_fault_injection_arm(KERNEL_FAULT_POINT_AP_STARTUP, 0));
    CHECK(!smp_host_prepare_start(1));
    CHECK(cpu_record(1)->lifecycle == CPU_STATE_PREPARED);
    kernel_fault_injection_get_snapshot(&faults);
    CHECK(faults.points[KERNEL_FAULT_POINT_AP_STARTUP].failures == 1);
    kernel_fault_injection_reset();
    CHECK(smp_host_prepare_start(1));
    CHECK(cpu_record(1)->lifecycle == CPU_STATE_STARTING);
    CHECK(smp_host_timeout_start(1));
    CHECK(cpu_record(1)->lifecycle == CPU_STATE_FAILED);
    CHECK(ap->online == 0);
    CHECK(ap->kernel_stack_top == stack_top);
    CHECK(cpu_topology_stats()->online_count == 1);
    CHECK(smp_startup_stats()->attempted == 1);
    CHECK(smp_startup_stats()->online == 0);
    CHECK(smp_startup_stats()->failed == 1);
    CHECK(smp_startup_stats()->last_failed_logical_id == 1);
    CHECK(!smp_host_prepare_start(1));
    CHECK(!smp_host_timeout_start(1));
    CHECK(smp_startup_stats()->attempted == 1);
    CHECK(smp_startup_stats()->failed == 1);
    CHECK(!cpu_transition(1, CPU_STATE_ONLINE));

    AddressSpace space = {};
    space.root_phys = 0x1000;
    space.identity = 1;
    uint32_t acknowledged = 0;
    CHECK(kernel_fault_injection_arm(KERNEL_FAULT_POINT_TLB_REQUEST, 0));
    CHECK(!smp_tlb_shootdown(&space, 0x4000, 1, 0, 1, 1, 1,
                             &acknowledged));
    CHECK(acknowledged == 0);
    kernel_fault_injection_get_snapshot(&faults);
    CHECK(faults.points[KERNEL_FAULT_POINT_TLB_REQUEST].failures == 1);
    return failures == 0 ? 0 : 1;
}
'''


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    trampoline = root / "arch/x86_64/ap_trampoline.asm"
    source_text = trampoline.read_text(encoding="utf-8")
    for marker in (
        "[ORG 0x7000]",
        "or eax, (1 << 8) | (1 << 11)",
        "mov rsp, [MAILBOX_STACK_TOP]",
        "mov edi, [MAILBOX_LOGICAL_ID]",
    ):
        if marker not in source_text:
            raise SystemExit(f"trampoline contract missing: {marker}")

    with tempfile.TemporaryDirectory(prefix="os64-ap-startup-") as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-DOS64_HOST_TEST", f"-I{root / 'include'}",
            str(source),
            str(root / "kernel/cpu/cpu.cpp"),
            str(root / "kernel/cpu/cpu_local.cpp"),
            str(root / "kernel/cpu/smp.cpp"),
            str(root / "kernel/debug/fault_injection.cpp"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("AP startup timeout/state contracts OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
