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
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/drv_format.h"
#include "kernel/fault_injection.h"

static int failures;
static volatile uint32_t thread_failures;
static void check(int value) { if (!value) failures++; }

static DrvManifest manifest(const char* name) {
    DrvManifest value = {};
    for (uint32_t i = 0; name[i] && i + 1 < sizeof(value.name); i++)
        value.name[i] = name[i];
    value.version[0] = '1';
    value.entry_symbol[0] = 'e';
    value.boot_modes = DRV_BOOT_NORMAL;
    return value;
}

int main() {
    driver_manager_init();
    driver_allocation_init();
    DrvManifest alpha_manifest = manifest("alpha");
    DrvManifest beta_manifest = manifest("beta");
    check(driver_manager_register_package_manifest(&alpha_manifest, 0) == 0);
    check(driver_manager_register_package_manifest(&beta_manifest, 0) == 0);
    DriverIdentity alpha = driver_manager_identity_from_name("alpha");
    DriverIdentity beta = driver_manager_identity_from_name("beta");
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_LOADING) == 0);
    check(driver_manager_set_state_identity(beta, DRIVER_STATE_LOADING) == 0);

    DriverAllocationResult normal;
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   257, 64, DRIVER_ALLOC_ZERO, "normal",
                                   &normal) == 0);
    check(((uintptr_t)normal.address & 63u) == 0 && normal.size == 257);
    for (uint32_t i = 0; i < 257; i++)
        check(((uint8_t*)normal.address)[i] == 0);
    check(driver_allocation_release(beta, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                    normal.handle) == DRIVER_LOAD_RESOURCE_DENIED);
    check(driver_allocation_release(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                    normal.handle) == 0);
    check(driver_allocation_release(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                    normal.handle) == DRIVER_LOAD_RESOURCE_DENIED);

    DriverAllocationResult pages;
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   5000, 8192,
                                   DRIVER_ALLOC_ZERO | DRIVER_ALLOC_PAGES,
                                   "pages", &pages) == 0);
    check(((uintptr_t)pages.address & 8191u) == 0);
    check(driver_allocation_release(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                    pages.handle) == 0);

    kernel_fault_injection_reset();
    check(kernel_fault_injection_arm(KERNEL_FAULT_POINT_DRIVER_ALLOC, 0));
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   5000, 4096, DRIVER_ALLOC_PAGES,
                                   "fault-page", &pages) ==
          DRIVER_LOAD_OUT_OF_MEMORY);
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   5000, 4096, DRIVER_ALLOC_PAGES,
                                   "fault-retry", &pages) == 0);
    check(driver_allocation_release(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                    pages.handle) == 0);

    DriverAllocationResult whole_budget, over_budget;
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   DRIVER_ALLOCATION_BUDGET_BYTES, 16,
                                   DRIVER_ALLOC_ZERO, "budget",
                                   &whole_budget) == 0);
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   1, 1, 0, "over", &over_budget) ==
          DRIVER_LOAD_ALLOCATION_BUDGET);
    check(driver_allocation_release(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                    whole_budget.handle) == 0);

    DriverAllocationResult atomic[DRIVER_ATOMIC_SLOT_COUNT];
    for (uint32_t i = 0; i < DRIVER_ATOMIC_SLOT_COUNT; i++) {
        check(driver_allocation_create(alpha, DRIVER_CONTEXT_IRQ, 64, 16,
                                       DRIVER_ALLOC_ATOMIC, "irq",
                                       &atomic[i]) == 0);
    }
    DriverAllocationResult ninth;
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_IRQ, 64, 16,
                                   DRIVER_ALLOC_ATOMIC, "irq-full", &ninth) ==
          DRIVER_LOAD_OUT_OF_MEMORY);
    DriverAllocationResult illegal;
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_IRQ, 64, 16, 0,
                                   "irq-sleep", &illegal) ==
          DRIVER_LOAD_CONTEXT_DENIED);
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_IRQ, 4096, 4096,
                                   DRIVER_ALLOC_PAGES, "irq-page", &illegal) ==
          DRIVER_LOAD_CONTEXT_DENIED);
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_EMERGENCY, 32, 8,
                                   DRIVER_ALLOC_ATOMIC, "nmi", &illegal) ==
          DRIVER_LOAD_CONTEXT_DENIED);
    for (uint32_t i = 0; i < DRIVER_ATOMIC_SLOT_COUNT; i++) {
        check(driver_allocation_release(alpha, DRIVER_CONTEXT_IRQ,
                                        atomic[i].handle) == 0);
    }

    DriverExecutionToken execution = {};
    check(driver_execution_enter(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                 &execution) == 0);
    DriverExecutionContext current;
    check(driver_execution_current(&current));
    check(driver_identity_equal(current.owner, alpha));
    check(current.kind == DRIVER_CONTEXT_THREAD_SLEEPABLE && current.depth == 1);
    DriverAllocationResult current_alloc;
    check(driver_allocation_create_current(96, 32, DRIVER_ALLOC_ZERO,
                                           "current", &current_alloc) == 0);
    check(driver_allocation_release_current(current_alloc.handle) == 0);
    driver_execution_leave(&execution);
    check(!driver_execution_current(&current));
    check(driver_allocation_create_current(16, 8, 0, "none", &current_alloc) ==
          DRIVER_LOAD_CONTEXT_DENIED);

    DriverAllocationResult leaked_a, leaked_b;
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   80, 16, 0, "leak-a", &leaked_a) == 0);
    check(driver_allocation_create(alpha, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   4096, 4096, DRIVER_ALLOC_PAGES,
                                   "leak-b", &leaked_b) == 0);
    check(driver_allocation_release_owner(alpha) == 2);

    DriverAllocationHandle capacity[DRIVER_MAX_ALLOCATIONS];
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        DriverAllocationResult allocation;
        check(driver_allocation_create(beta, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                       1, 1, 0, "capacity",
                                       &allocation) == 0);
        capacity[i] = allocation.handle;
    }
    DriverAllocationResult capacity_overflow;
    check(driver_allocation_create(beta, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   1, 1, 0, "capacity-overflow",
                                   &capacity_overflow) == DRIVER_LOAD_NO_SLOT);
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        check(driver_allocation_release(beta, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                        capacity[i]) == 0);
    }

    DriverIdentity stale = {alpha.slot, alpha.generation + 100u};
    check(driver_allocation_create(stale, DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                   8, 8, 0, "stale", &capacity_overflow) ==
          DRIVER_LOAD_STALE_IDENTITY);

    std::vector<std::thread> workers;
    for (uint32_t worker = 0; worker < 4; worker++) {
        workers.emplace_back([&]() {
            for (uint32_t cycle = 0; cycle < 250; cycle++) {
                DriverAllocationResult allocation;
                if (driver_allocation_create(beta,
                                             DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                             48 + (cycle & 31u), 16,
                                             DRIVER_ALLOC_ZERO, "parallel",
                                             &allocation) != 0 ||
                    driver_allocation_release(beta,
                                              DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                              allocation.handle) != 0) {
                    __atomic_add_fetch(&thread_failures, 1u, __ATOMIC_RELAXED);
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(thread_failures == 0);

    DriverAllocationStats stats;
    driver_allocation_get_stats(&stats);
    check(stats.active == 0 && stats.active_bytes == 0);
    check(stats.atomic_active == 0 && stats.quarantined == 0);
    check(stats.atomic_exhaustion == 1);
    check(stats.owner_rejections == 1 && stats.stale_rejections == 1);
    check(stats.budget_rejections >= 1 && stats.context_rejections == 3);
    check(stats.automatic_releases == 2);
    check(stats.exhaustion_failures == 1);
    DriverResourceStats resources;
    driver_resource_get_stats(&resources);
    check(resources.active == 0);
    return failures == 0 ? 0 : 1;
}
"""

STUBS = r"""
#include <stdint.h>
#include <stdlib.h>
#include "kernel/driver/driver_manager.h"
int strcmp64(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
void copy_string64(char* out, uint32_t capacity, const char* text) {
    uint32_t i = 0;
    if (!out || !capacity) return;
    if (text) for (; text[i] && i + 1 < capacity; i++) out[i] = text[i];
    out[i] = 0;
}
extern "C" void* kmalloc(size_t size) { return malloc(size); }
extern "C" void kfree(void* pointer) { free(pointer); }
void driver_image_va_init() {}
void driver_manager_binding_init() {}
void driver_irq_init() {}
void driver_export_init() {}
DriverDeviceIdentity driver_device_identity_invalid() {
    DriverDeviceIdentity identity = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return identity;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_driver_alloc_") as temp:
        path = Path(temp)
        source = path / "test.cpp"
        stubs = path / "stubs.cpp"
        binary = path / "test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        stubs.write_text(textwrap.dedent(STUBS), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/driver/driver_manager.cpp"),
            str(ROOT / "kernel/driver/driver_resource.cpp"),
            str(ROOT / "kernel/driver/driver_alloc.cpp"),
            str(ROOT / "kernel/debug/fault_injection.cpp"),
            str(source), str(stubs), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("driver owned allocation, budget, page, and cleanup test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
