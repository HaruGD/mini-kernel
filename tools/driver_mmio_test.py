#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <stdint.h>
#include <stdio.h>
#include "kernel/driver/driver_mmio.h"
#include "kernel/driver/drv_format.h"
#include "kernel/pci.h"

static int failures;
#define check(value) do { if (!(value)) { failures++; \
    fprintf(stderr, "check failed at line %d\n", __LINE__); } } while (0)
static DrvManifest manifest(const char* name) {
    DrvManifest value = {};
    for (uint32_t i = 0; name[i] && i + 1 < sizeof(value.name); i++)
        value.name[i] = name[i];
    value.version[0] = '1'; value.entry_symbol[0] = 'e';
    value.boot_modes = DRV_BOOT_NORMAL;
    value.permissions = DRV_PERMISSION_PCI | DRV_PERMISSION_MMIO;
    return value;
}

extern uint32_t test_bar_type;
extern uint64_t test_bar_size;

int main() {
    driver_manager_init();
    driver_mmio_init();
    DrvManifest am = manifest("alpha"), bm = manifest("beta");
    check(driver_manager_register_package_manifest(&am, 0) == 0);
    check(driver_manager_register_package_manifest(&bm, 0) == 0);
    DriverIdentity alpha = driver_manager_identity_from_name("alpha");
    DriverIdentity beta = driver_manager_identity_from_name("beta");
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_LOADING) == 0);
    check(driver_manager_set_state_identity(beta, DRIVER_STATE_LOADING) == 0);
    const PCIDeviceInfo* pci = pci_get_device(0);
    check(driver_manager_bind_pci("alpha", pci, 0) == 0);
    DriverDeviceIdentity device;
    check(driver_manager_bound_pci_identity(alpha, pci, &device));

    DriverMmioMapping first, shared;
    check(driver_mmio_map(alpha, device, 0, 3, 8192,
                          DRIVER_MMIO_CACHE_DEVICE_UC, &first) == 0);
    check(driver_mmio_map(alpha, device, 0, 3, 8192,
                          DRIVER_MMIO_CACHE_DEVICE_UC, &shared) == 0);
    check(first.handle.slot == shared.handle.slot &&
          first.handle.generation == shared.handle.generation);
    check(driver_mmio_write(alpha, first.handle, 4, 4, 0xA5A55A5Au) == 0);
    uint64_t value = 0;
    check(driver_mmio_read(alpha, first.handle, 4, 4, &value) == 0 &&
          value == 0xA5A55A5Au);
    check(driver_mmio_read(alpha, first.handle, 3, 4, &value) ==
          DRIVER_LOAD_MMIO_RANGE);
    check(driver_mmio_read(alpha, first.handle, 8190, 4, &value) ==
          DRIVER_LOAD_MMIO_RANGE);
    check(driver_mmio_read(beta, first.handle, 4, 4, &value) ==
          DRIVER_LOAD_MMIO_DENIED);
    DriverDeviceIdentity stale = {device.slot, device.generation + 1};
    DriverMmioMapping rejected;
    check(driver_mmio_map(alpha, stale, 0, 0, 16,
                          DRIVER_MMIO_CACHE_DEVICE_UC, &rejected) ==
          DRIVER_LOAD_MMIO_DENIED);
    check(driver_mmio_map(alpha, device, 0, 0, 16,
                          DRIVER_MMIO_CACHE_WRITE_COMBINING, &rejected) ==
          DRIVER_LOAD_MMIO_CACHE);
    check(driver_mmio_map(alpha, device, 0, test_bar_size - 4, 8,
                          DRIVER_MMIO_CACHE_DEVICE_UC, &rejected) ==
          DRIVER_LOAD_MMIO_RANGE);
    test_bar_type = PCI_BAR_TYPE_IO;
    check(driver_mmio_map(alpha, device, 0, 0, 16,
                          DRIVER_MMIO_CACHE_DEVICE_UC, &rejected) ==
          DRIVER_LOAD_MMIO_RANGE);
    test_bar_type = PCI_BAR_TYPE_MMIO32;

    check(driver_mmio_unmap(alpha, first.handle) == 0);
    check(driver_mmio_read(alpha, shared.handle, 4, 4, &value) == 0);
    check(driver_mmio_unmap(alpha, shared.handle) == 0);
    check(driver_mmio_read(alpha, shared.handle, 4, 4, &value) ==
          DRIVER_LOAD_MMIO_DENIED);
    check(driver_mmio_unmap(alpha, shared.handle) == DRIVER_LOAD_MMIO_DENIED);

    for (uint32_t cycle = 0; cycle < 256; cycle++) {
        DriverMmioMapping mapping;
        check(driver_mmio_map(alpha, device, 0, cycle & 7u,
                              4096 + (cycle & 15u),
                              DRIVER_MMIO_CACHE_DEVICE_UC, &mapping) == 0);
        check(driver_mmio_unmap(alpha, mapping.handle) == 0);
    }
    DriverMmioStats stats;
    driver_mmio_get_stats(&stats);
    check(stats.active == 0 && stats.free_pages == stats.arena_pages);
    check(stats.quarantined_pages == 0 && stats.shared_maps == 1);
    DriverResourceStats resources;
    driver_resource_get_stats(&resources);
    check(resources.by_kind[DRIVER_RESOURCE_MMIO] == 0);
    return failures == 0 ? 0 : 1;
}
'''

STUBS = r'''
#include <stdint.h>
#include "kernel/pci.h"
#include "kernel/driver/driver_alloc.h"
uint32_t test_bar_type = PCI_BAR_TYPE_MMIO32;
uint64_t test_bar_size = 0x20000;
static PCIDeviceInfo device = {0x1234, 0x1111, 0, 0, 0, 2, 0, 0,
                               0, 0, 3, 0, 0, 10, 1, 1, {0,0,0},
                               {0x10000000,0,0,0,0,0}};
int strcmp64(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
void copy_string64(char* out, uint32_t cap, const char* text) {
    uint32_t i = 0; if (!out || !cap) return;
    if (text) for (; text[i] && i + 1 < cap; i++) out[i] = text[i];
    out[i] = 0;
}
uint32_t pci_get_device_count() { return 1; }
const PCIDeviceInfo* pci_get_device(uint32_t index) { return index ? 0 : &device; }
int pci_get_bar(const PCIDeviceInfo*, uint32_t index, PCIBarInfo* out) {
    if (index || !out) return 0;
    out->base = 0x10000003; out->size = test_bar_size;
    out->type = test_bar_type; out->flags = 0; return 1;
}
int pci_enable_memory_space(const PCIDeviceInfo*) { return 1; }
void driver_image_va_init() {}
void driver_irq_init() {}
void driver_export_init() {}
static DriverExecutionContext current;
int driver_execution_current(DriverExecutionContext* out) {
    if (!current.depth) return 0;
    if (out) *out = current;
    return 1;
}
int driver_execution_enter(DriverIdentity owner, uint32_t kind,
                           DriverExecutionToken* token) {
    if (token) token->active = 1;
    current.owner = owner;
    current.kind = kind; current.depth = 1; return 0;
}
void driver_execution_leave(DriverExecutionToken*) { current = {}; }
'''

def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_driver_mmio_") as temp:
        path = Path(temp)
        source = path / "test.cpp"; stubs = path / "stubs.cpp"
        binary = path / "test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        stubs.write_text(textwrap.dedent(STUBS), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-DOS64_DRIVER_HOST_TEST",
            "-I", str(ROOT / "include"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/driver/driver_manager.cpp"),
            str(ROOT / "kernel/driver/driver_resource.cpp"),
            str(ROOT / "kernel/driver/driver_binding.cpp"),
            str(ROOT / "kernel/driver/driver_mmio.cpp"),
            str(source), str(stubs), "-o", str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("driver MMIO capability and reusable VA test OK")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
