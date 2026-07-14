#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>

#include "kernel/fault_injection.h"
#include "kernel/graphics/surface_backing.h"
#include "kernel/mm/vm.h"
#include "kernel_mm_host_stubs.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static int page_is_zero(uint64_t phys) {
    const uint8_t* bytes = (const uint8_t*)(uintptr_t)phys;
    for (uint32_t i = 0; i < (uint32_t)VM_PAGE_SIZE; i++) {
        if (bytes[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void check_empty() {
    KernelGraphicsSurfaceBackingStats stats;
    kernel_graphics_surface_backing_get_stats(&stats);
    check(stats.active_backings == 0);
    check(stats.mapped_pages == 0);
    check(stats.mapped_bytes == 0);
    check(host_mm_allocated_pages() == 0);
    check(host_mm_mapped_pages() == 0);
}

int main() {
    host_mm_reset();
    kernel_fault_injection_reset();
    kernel_graphics_surface_backing_init();
    check_empty();

    uint32_t* pixels = 0;
    uint32_t pages = 0;
    check(kernel_graphics_surface_backing_allocate(0, 4097, &pixels, &pages) == 1);
    check(pixels == (uint32_t*)(uintptr_t)VM_KERNEL_SURFACE_BASE);
    check(pages == 2);
    check(host_mm_allocated_pages() == 2);
    check(host_mm_mapped_pages() == 2);
    KernelGraphicsSurfaceBackingInfo info;
    KernelGraphicsSurfaceBackingStats stats;
    check(kernel_graphics_surface_backing_get_info(0, &info) == 1);
    check(info.active == 1 && info.page_count == 2 && info.byte_size == 4097);
    check(info.virtual_base == VM_KERNEL_SURFACE_BASE);
    check(page_is_zero(kernel_graphics_surface_backing_get_phys(0, 0)));
    check(page_is_zero(kernel_graphics_surface_backing_get_phys(0, 1)));
    check(kernel_graphics_surface_backing_allocate(0, 1, &pixels, &pages) == 0);
    kernel_graphics_surface_backing_release(0);
    check_empty();

    check(kernel_graphics_surface_backing_allocate(0, 2u * VM_PAGE_SIZE,
                                                    &pixels, &pages) == 1);
    host_mm_fail_unmap_after(1);
    kernel_graphics_surface_backing_release(0);
    kernel_graphics_surface_backing_get_stats(&stats);
    check(stats.active_backings == 1 && stats.mapped_pages == 1 &&
          stats.unmap_failures == 1);
    check(host_mm_allocated_pages() == 1 && host_mm_mapped_pages() == 1);
    kernel_graphics_surface_backing_release(0);
    check_empty();
    kernel_graphics_surface_backing_init();

    check(kernel_graphics_surface_backing_allocate(
              KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS, 1, &pixels, &pages) == 0);
    check(kernel_graphics_surface_backing_allocate(0, 0, &pixels, &pages) == 0);
    check(kernel_graphics_surface_backing_allocate(0, 1, 0, &pages) == 0);
    check(kernel_graphics_surface_backing_allocate(0, 1, &pixels, 0) == 0);
    check(kernel_graphics_surface_backing_allocate(
              0,
              (KERNEL_GRAPHICS_SURFACE_MAX_PAGES + 1u) * (uint32_t)VM_PAGE_SIZE,
              &pixels,
              &pages) == 0);
    check_empty();

    kernel_fault_injection_arm(KERNEL_FAULT_POINT_PMM, 1);
    check(kernel_graphics_surface_backing_allocate(0, 2u * VM_PAGE_SIZE,
                                                    &pixels, &pages) == 0);
    check_empty();

    host_mm_fail_map_after(1);
    check(kernel_graphics_surface_backing_allocate(0, 2u * VM_PAGE_SIZE,
                                                    &pixels, &pages) == 0);
    check_empty();

    check(kernel_graphics_surface_backing_allocate(0, 3u * VM_PAGE_SIZE,
                                                    &pixels, &pages) == 1);
    check(kernel_graphics_surface_backing_allocate(1, 2u * VM_PAGE_SIZE,
                                                    &pixels, &pages) == 0);
    check(kernel_graphics_surface_backing_allocate(1, VM_PAGE_SIZE,
                                                    &pixels, &pages) == 1);
    kernel_graphics_surface_backing_get_stats(&stats);
    check(stats.active_backings == 2);
    check(stats.mapped_pages == KERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES);
    check(stats.mapped_bytes ==
          (uint64_t)KERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES * VM_PAGE_SIZE);
    check(stats.allocation_failures >= 6);
    check(stats.rollback_pages == 2);
    check(stats.unmap_failures == 0);
    kernel_graphics_surface_backing_release(1);
    kernel_graphics_surface_backing_release(0);
    check_empty();

    kernel_graphics_surface_backing_init();
    host_mm_reset();
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_surface_backing_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "surface_backing_test.cpp"
        binary_path = temp_path / "surface_backing_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DOS64_HOST_TEST",
            "-DKERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES=4",
            "-I",
            str(REPO_ROOT / "include"),
            "-I",
            str(REPO_ROOT / "tools/fixtures"),
            str(REPO_ROOT / "kernel/debug/fault_injection.cpp"),
            str(REPO_ROOT / "kernel/graphics/surface_backing.cpp"),
            str(REPO_ROOT / "tools/fixtures/kernel_mm_host_stubs.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("surface backing test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
