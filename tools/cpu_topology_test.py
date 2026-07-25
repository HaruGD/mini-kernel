#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "kernel/acpi.h"
#include "kernel/acpi_madt.h"
#include "kernel/cpu.h"

static int failures = 0;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value); failures++; } } while (0)

static void lapic(uint8_t* out, uint8_t acpi, uint8_t apic, uint32_t flags) {
    out[0] = 0; out[1] = 8; out[2] = acpi; out[3] = apic;
    out[4] = flags; out[5] = flags >> 8; out[6] = flags >> 16; out[7] = flags >> 24;
}

static void x2apic(uint8_t* out, uint32_t apic, uint32_t flags, uint32_t uid) {
    for (uint32_t i = 0; i < 16; i++) out[i] = 0;
    out[0] = 9; out[1] = 16;
    for (uint32_t i = 0; i < 4; i++) {
        out[4 + i] = apic >> (i * 8);
        out[8 + i] = flags >> (i * 8);
        out[12 + i] = uid >> (i * 8);
    }
}

int main() {
    AcpiState acpi = {};
    uint8_t one[8]; lapic(one, 7, 3, 1);
    CHECK(acpi_madt_parse_cpu_entries(one, sizeof(one), &acpi));
    CHECK(acpi.cpu_count == 1 && acpi.cpus[0].apic_id == 3);
    CHECK(cpu_topology_init(&acpi, 3));
    CHECK(cpu_topology_stats()->record_count == 1);
    CHECK(cpu_topology_stats()->online_count == 1);
    CHECK(cpu_record(0)->is_bsp && cpu_record(0)->lifecycle == CPU_STATE_ONLINE);

    uint8_t four[32];
    for (uint8_t i = 0; i < 4; i++) lapic(four + i * 8, i, i, 1);
    CHECK(acpi_madt_parse_cpu_entries(four, sizeof(four), &acpi));
    CHECK(cpu_topology_init(&acpi, 2));
    CHECK(cpu_topology_stats()->record_count == 4);
    CHECK(cpu_record(0)->apic_id == 2 && cpu_record(0)->logical_id == 0);
    CHECK(cpu_topology_stats()->online_count == 1);
    CHECK(cpu_topology_stats()->discovered_count == 3);
    CHECK(cpu_transition_is_legal(CPU_STATE_DISCOVERED, CPU_STATE_PREPARED));
    CHECK(!cpu_transition_is_legal(CPU_STATE_DISCOVERED, CPU_STATE_ONLINE));

    uint8_t disabled[16];
    lapic(disabled, 0, 0, 1); lapic(disabled + 8, 1, 1, 0);
    CHECK(acpi_madt_parse_cpu_entries(disabled, sizeof(disabled), &acpi));
    CHECK(acpi.cpu_count == 1 && acpi.cpu_disabled_count == 1);

    uint8_t duplicate[16];
    lapic(duplicate, 0, 0, 1); lapic(duplicate + 8, 1, 0, 1);
    CHECK(acpi_madt_parse_cpu_entries(duplicate, sizeof(duplicate), &acpi));
    CHECK(!cpu_topology_init(&acpi, 0));
    CHECK(cpu_topology_stats()->duplicate_count == 1);

    uint8_t x2[24];
    lapic(x2, 0, 0, 1); x2apic(x2 + 8, 0x102, 1, 9);
    CHECK(acpi_madt_parse_cpu_entries(x2, sizeof(x2), &acpi));
    CHECK(acpi.cpu_unsupported_count == 1);
    CHECK(cpu_topology_init(&acpi, 0));
    CHECK(cpu_topology_stats()->record_count == 1);

    uint8_t overflow[72];
    for (uint8_t i = 0; i < 9; i++) lapic(overflow + i * 8, i, i, 1);
    CHECK(acpi_madt_parse_cpu_entries(overflow, sizeof(overflow), &acpi));
    CHECK(acpi.cpu_count == ACPI_MAX_CPUS && acpi.cpu_overflow_count == 1);
    CHECK(!cpu_topology_init(&acpi, 0));

    uint8_t malformed[3] = {0, 8, 0};
    CHECK(!acpi_madt_parse_cpu_entries(malformed, sizeof(malformed), &acpi));
    CHECK(acpi.cpu_count == 0 && acpi.cpu_malformed_count == 1);

    lapic(one, 7, 3, 1);
    CHECK(acpi_madt_parse_cpu_entries(one, sizeof(one), &acpi));
    CHECK(!cpu_topology_init(&acpi, 4));
    CHECK(cpu_topology_stats()->bsp_mismatch_count == 1);
    return failures == 0 ? 0 : 1;
}
'''


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="os64-cpu-topology-") as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(HARNESS, encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-DOS64_HOST_TEST", f"-I{root / 'include'}",
            str(source),
            str(root / "kernel/acpi/madt_cpu.cpp"),
            str(root / "kernel/cpu/cpu.cpp"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("CPU topology contracts OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
