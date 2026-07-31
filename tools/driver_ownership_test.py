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
#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/pci.h"

static int failures = 0;
static uint32_t irq_calls = 0;
static volatile uint32_t concurrent_failures = 0;

static void check(int condition) { if (!condition) failures++; }
static uint64_t test_irq(uint64_t irq) { irq_calls += (uint32_t)irq; return 0; }
static uint64_t test_export() { return 0x47A; }

static DrvManifest manifest(const char* name, uint32_t permissions) {
    DrvManifest value = {};
    for (uint32_t i = 0; name[i] != '\0' && i + 1 < sizeof(value.name); i++) {
        value.name[i] = name[i];
    }
    value.version[0] = '1';
    value.entry_symbol[0] = 'e';
    value.permissions = permissions;
    value.boot_modes = DRV_BOOT_NORMAL;
    return value;
}

int main() {
    driver_manager_init();
    DrvManifest alpha_manifest = manifest(
        "alpha", DRV_PERMISSION_PCI | DRV_PERMISSION_INTERRUPT);
    check(driver_manager_register_package_manifest(&alpha_manifest, 0) == DRIVER_LOAD_OK);
    DriverIdentity alpha = driver_manager_identity_from_name("alpha");
    check(driver_identity_is_valid(alpha));
    check(driver_manager_identity_is_live(alpha));
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_LOADING) == DRIVER_LOAD_OK);
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_READY) == DRIVER_LOAD_STATE_DENIED);
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_LINKED) == DRIVER_LOAD_OK);
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_READY) == DRIVER_LOAD_OK);

    check(driver_manager_register_builtin("beta", "1", DRIVER_KIND_CORE,
                                          DRV_PERMISSION_PCI, 0) == DRIVER_LOAD_OK);
    check(driver_manager_set_state("beta", DRIVER_STATE_READY) == DRIVER_LOAD_OK);
    DriverIdentity beta = driver_manager_identity_from_name("beta");
    check(driver_identity_is_valid(beta) && !driver_identity_equal(alpha, beta));

    DriverResourceHandle custom;
    check(driver_resource_register(alpha, driver_device_identity_invalid(),
                                   DRIVER_RESOURCE_ALLOCATION, 3, 0x1234, 64,
                                   "owned", &custom) == DRIVER_LOAD_OK);
    check(driver_resource_resolve(alpha, custom, DRIVER_RESOURCE_ALLOCATION) != 0);
    check(driver_resource_resolve(beta, custom, DRIVER_RESOURCE_ALLOCATION) == 0);
    check(driver_resource_release(beta, custom, DRIVER_RESOURCE_ALLOCATION) ==
          DRIVER_LOAD_RESOURCE_DENIED);

    PCIDeviceInfo device = {};
    device.vendor_id = 0x1234;
    device.device_id = 0x5678;
    device.bus = 0;
    device.device = 2;
    device.function = 0;
    device.class_code = 2;
    device.bar_count = 1;
    check(driver_manager_bind_pci("alpha", &device, 0) == DRIVER_LOAD_OK);
    check(driver_manager_binding_count() == 1);
    const DriverBindingRecord* binding = driver_manager_binding_get(0);
    check(binding != 0 && driver_identity_equal(binding->owner, alpha));
    DriverDeviceIdentity device_identity = {
        binding != 0 ? binding->slot : DRIVER_IDENTITY_INVALID_SLOT,
        binding != 0 ? binding->generation : 0
    };
    check(driver_manager_device_identity_is_live(device_identity, alpha));
    check(!driver_manager_device_identity_is_live(device_identity, beta));
    check(driver_manager_bind_pci("beta", &device, 0) == DRIVER_LOAD_BIND_DENIED);

    check(driver_irq_register_handler("alpha", 5, test_irq, 0) == DRIVER_LOAD_OK);
    driver_irq_dispatch(5);
    check(irq_calls == 5 && driver_irq_hook_count() == 1);
    check(driver_export_register("alpha", "ping", (void*)test_export, 0) == DRIVER_LOAD_OK);
    check(driver_export_resolve("alpha", "ping", 0) == (void*)test_export);

    DriverResourceStats stats;
    driver_resource_get_stats(&stats);
    check(stats.active == 4);
    check(stats.by_kind[DRIVER_RESOURCE_EXPORT] == 1);
    check(stats.by_kind[DRIVER_RESOURCE_PCI_BINDING] == 1);
    check(stats.by_kind[DRIVER_RESOURCE_IRQ_HOOK] == 1);
    check(stats.by_kind[DRIVER_RESOURCE_ALLOCATION] == 1);
    check(stats.owner_rejections == 1);

    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_QUIESCING) == DRIVER_LOAD_OK);
    DriverResourceHandle denied;
    check(driver_resource_register(alpha, driver_device_identity_invalid(),
                                   DRIVER_RESOURCE_ALLOCATION, 0, 0, 1,
                                   "late", &denied) == DRIVER_LOAD_STATE_DENIED);
    check(driver_export_resolve("alpha", "ping", 0) == 0);
    driver_irq_dispatch(5);
    check(irq_calls == 5);

    driver_export_unregister_module("alpha");
    driver_irq_unregister_module("alpha");
    driver_manager_unbind_module("alpha");
    check(driver_manager_unregister("alpha") == DRIVER_LOAD_OK);
    check(!driver_manager_identity_is_live(alpha));
    driver_resource_get_stats(&stats);
    check(stats.active == 0);
    check(driver_resource_resolve(alpha, custom, DRIVER_RESOURCE_ALLOCATION) == 0);

    std::vector<std::thread> workers;
    for (uint32_t worker = 0; worker < 4; worker++) {
        workers.emplace_back([&]() {
            for (uint32_t cycle = 0; cycle < 1000; cycle++) {
                DriverResourceHandle handle;
                if (driver_resource_register(beta,
                                             driver_device_identity_invalid(),
                                             DRIVER_RESOURCE_ALLOCATION,
                                             0, cycle, 32, "concurrent",
                                             &handle) != DRIVER_LOAD_OK ||
                    driver_resource_release(beta, handle,
                                            DRIVER_RESOURCE_ALLOCATION) !=
                        DRIVER_LOAD_OK) {
                    __atomic_add_fetch(&concurrent_failures, 1u,
                                       __ATOMIC_RELAXED);
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(concurrent_failures == 0);
    driver_resource_get_stats(&stats);
    check(stats.active == 0);

    check(driver_manager_register_package_manifest(&alpha_manifest, 0) == DRIVER_LOAD_OK);
    DriverIdentity alpha_reused = driver_manager_identity_from_name("alpha");
    check(alpha_reused.slot == alpha.slot);
    check(alpha_reused.generation != alpha.generation);
    check(!driver_manager_identity_is_live(alpha));
    check(driver_manager_identity_is_live(alpha_reused));
    check(driver_manager_set_state_identity(alpha, DRIVER_STATE_LOADING) ==
          DRIVER_LOAD_STALE_IDENTITY);

    DriverResourceHandle handles[DRIVER_MAX_RESOURCES];
    for (uint32_t i = 0; i < DRIVER_MAX_RESOURCES; i++) {
        check(driver_resource_register(beta, driver_device_identity_invalid(),
                                       DRIVER_RESOURCE_ALLOCATION, 0, i, i + 1,
                                       "capacity", &handles[i]) == DRIVER_LOAD_OK);
    }
    DriverResourceHandle overflow;
    check(driver_resource_register(beta, driver_device_identity_invalid(),
                                   DRIVER_RESOURCE_ALLOCATION, 0, 0, 1,
                                   "overflow", &overflow) == DRIVER_LOAD_NO_SLOT);
    check(driver_resource_release_owner(beta) == DRIVER_MAX_RESOURCES);
    driver_resource_get_stats(&stats);
    check(stats.active == 0 && stats.high_water == DRIVER_MAX_RESOURCES);
    check(stats.exhaustion_failures == 1);
    check(stats.state_rejections == 1);

    return failures == 0 ? 0 : 1;
}
"""


STUBS = r"""
#include <stdint.h>
#include "kernel/pci.h"

int strcmp64(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) { left++; right++; }
    return (unsigned char)*left - (unsigned char)*right;
}

void copy_string64(char* out, uint32_t capacity, const char* text) {
    if (out == 0 || capacity == 0) return;
    uint64_t i = 0;
    if (text != 0) {
        for (; text[i] != '\0' && i + 1 < capacity; i++) out[i] = text[i];
    }
    out[i] = '\0';
}

uint32_t pci_get_device_count() { return 0; }
const PCIDeviceInfo* pci_get_device(uint32_t) { return 0; }
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_driver_ownership_") as temp:
        temp_path = Path(temp)
        source = temp_path / "driver_ownership_test.cpp"
        stubs = temp_path / "driver_ownership_stubs.cpp"
        binary = temp_path / "driver_ownership_test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        stubs.write_text(textwrap.dedent(STUBS), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/driver/driver_manager.cpp"),
            str(ROOT / "kernel/driver/driver_resource.cpp"),
            str(ROOT / "kernel/driver/driver_va.cpp"),
            str(ROOT / "kernel/driver/driver_exports.cpp"),
            str(ROOT / "kernel/driver/driver_binding.cpp"),
            str(ROOT / "kernel/driver/driver_irq.cpp"),
            str(source), str(stubs), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("driver ownership and lifecycle test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
