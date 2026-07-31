#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <stdint.h>
#include "kernel/driver/driver_manager.h"
#include "kernel/driver/driver_va.h"
#include "kernel/driver/drv_format.h"

static int failures;
static void check(int condition) { if (!condition) failures++; }

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
    DrvManifest alpha_manifest = manifest("alpha");
    DrvManifest beta_manifest = manifest("beta");
    check(driver_manager_register_package_manifest(&alpha_manifest, 0) == 0);
    check(driver_manager_register_package_manifest(&beta_manifest, 0) == 0);
    DriverIdentity alpha = driver_manager_identity_from_name("alpha");
    DriverIdentity beta = driver_manager_identity_from_name("beta");
    const uint64_t base = 0x100000ULL;
    check(driver_image_va_reset_for_test(base, base + 16 * 4096, 1));

    DriverVaHandle first, second, reused;
    uint64_t first_base = 0, second_base = 0, reused_base = 0;
    check(driver_image_va_allocate(alpha, 2, &first, &first_base) == 0);
    check(first_base == base + 4096);
    check(driver_image_va_allocate(alpha, 3, &second, &second_base) == 0);
    check(second_base == base + 5 * 4096);
    check(driver_image_va_release(alpha, first) == 0);
    check(driver_image_va_allocate(alpha, 1, &reused, &reused_base) == 0);
    check(reused_base == first_base);
    check(driver_image_va_release(beta, reused) == DRIVER_LOAD_RESOURCE_DENIED);
    check(driver_image_va_release(alpha, reused) == 0);
    check(driver_image_va_release(alpha, reused) == DRIVER_LOAD_RESOURCE_DENIED);
    check(driver_image_va_release(alpha, second) == 0);

    DriverVaStats stats;
    driver_image_va_get_stats(&stats);
    check(stats.active == 0);
    check(stats.free_pages == 16 && stats.largest_free_pages == 16);
    check(stats.free_extents == 1);
    check(stats.owner_rejections == 1 && stats.stale_rejections == 1);

    DriverVaHandle whole, overflow;
    uint64_t whole_base = 0, overflow_base = 0;
    check(driver_image_va_allocate(alpha, 14, &whole, &whole_base) == 0);
    check(driver_image_va_allocate(alpha, 1, &overflow, &overflow_base) ==
          DRIVER_LOAD_OUT_OF_MEMORY);
    check(driver_image_va_release(alpha, whole) == 0);

    DriverVaHandle quarantined;
    uint64_t quarantined_base = 0;
    check(driver_image_va_allocate(alpha, 2, &quarantined,
                                   &quarantined_base) == 0);
    check(driver_image_va_quarantine(alpha, quarantined) == 0);
    check(driver_image_va_release(alpha, quarantined) ==
          DRIVER_LOAD_RESOURCE_DENIED);
    driver_image_va_get_stats(&stats);
    check(stats.quarantined == 1 && stats.active == 1);
    check(stats.free_pages == 12);
    check(stats.exhaustion_failures == 1);
    return failures == 0 ? 0 : 1;
}
"""

STUBS = r"""
#include <stdint.h>
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
void driver_resource_init() {}
void driver_manager_binding_init() {}
void driver_irq_init() {}
void driver_export_init() {}
uint32_t driver_resource_release_owner(DriverIdentity) { return 0; }
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_driver_va_") as temp:
        path = Path(temp)
        source = path / "test.cpp"
        stubs = path / "stubs.cpp"
        binary = path / "test"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        stubs.write_text(textwrap.dedent(STUBS), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/driver/driver_manager.cpp"),
            str(ROOT / "kernel/driver/driver_va.cpp"),
            str(source), str(stubs), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("driver reusable VA allocator test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
