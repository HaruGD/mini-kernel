#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]

TEST_SOURCE = r"""
#include <stdint.h>
#include <stdio.h>
#include "kernel/handle/kernel_objects.h"
#include "kernel/process_surface.h"
#include "kernel/syscall64.h"
#include "kernel_mm_host_stubs.h"
#include "os64/surface_types.h"

static int failures = 0;
static void check_at(int condition, int line) {
    if (!condition) { failures++; fprintf(stderr, "check failed at line %d\n", line); }
}
#define check(condition) check_at((condition), __LINE__)

static void init_process(Process* process, uint32_t pid) {
    process->pid = pid;
    process->permissions = OS_PROCESS_PERMISSION_SHARED_SURFACE;
    address_space_init(&process->address_space);
    kernel_handle_table_init(&process->handle_table);
    process_surface_mappings_reset(process);
}

int main() {
    host_mm_reset();
    kernel_objects_init();
    Process owner = {};
    Process receiver = {};
    init_process(&owner, 10);
    init_process(&receiver, 20);

    uint64_t surface = kernel_graphics_surface_create(
        &owner.handle_table, owner.pid, 1025, 1, OS64_PIXEL_FORMAT_RGB,
        OS_SURFACE_APPLICATION_RIGHTS);
    check(surface != 0);
    uint32_t kernel_mappings = host_mm_mapped_pages();
    check(kernel_mappings == 2);

    uint64_t address = process_surface_map(
        &owner, surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE);
    check(address == VM_USER_SURFACE_BASE);
    check(owner.active_surface_mapping_count == 1);
    check(process_surface_map(&owner, surface,
                              OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE) == address);
    check((int64_t)process_surface_map(&owner, surface, OS_SURFACE_MAP_READ) ==
          SYS_ERR_ALREADY_EXISTS);
    uint64_t first_flags = address_space_get_flags(&owner.address_space, address);
    uint64_t second_flags = address_space_get_flags(&owner.address_space,
                                                     address + VM_PAGE_SIZE);
    check((first_flags & (VM_FLAG_USER | VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE)) ==
          (VM_FLAG_USER | VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE));
    check((second_flags & (VM_FLAG_USER | VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE)) ==
          (VM_FLAG_USER | VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE));
    check(address_space_buffer_accessible(&owner.address_space, address, 4100, 1));
    check(process_surface_unmap(&owner, surface, address + VM_PAGE_SIZE) ==
          SYS_ERR_INVALID_ARGUMENT);
    check(owner.active_surface_mapping_count == 1);
    check(process_surface_unmap(&owner, surface, address) == 0);
    check(owner.active_surface_mapping_count == 0);
    check(host_mm_mapped_pages() == kernel_mappings);
    check(process_surface_unmap(&owner, surface, address) == SYS_ERR_NOT_FOUND);
    address = process_surface_map(&owner, surface,
                                  OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE);
    check(address == VM_USER_SURFACE_BASE);
    host_mm_fail_unmap_after(0);
    check(process_surface_unmap_all(&owner) == 0);
    check(owner.active_surface_mapping_count == 0);
    check(host_mm_mapped_pages() == kernel_mappings);

    KernelHandle source;
    check(kernel_handle_resolve_copy(&owner.handle_table, surface,
                                     KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                     KERNEL_HANDLE_RIGHT_TRANSFER,
                                     &source));
    check((int64_t)process_surface_map(&receiver, surface,
                                      OS_SURFACE_MAP_READ) ==
          SYS_ERR_PERMISSION_DENIED);
    uint64_t read_handle = kernel_object_clone_handle_with_rights(
        &receiver.handle_table, &source, OS_SURFACE_TRANSFER_RIGHTS);
    check(read_handle != 0);
    check(kernel_object_clone_handle_with_rights(
              &receiver.handle_table, &source,
              OS_SURFACE_TRANSFER_RIGHTS | KERNEL_HANDLE_RIGHT_ENUMERATE) == 0);
    uint64_t read_address = process_surface_map(&receiver, read_handle,
                                                OS_SURFACE_MAP_READ);
    check(read_address == VM_USER_SURFACE_BASE);
    uint64_t read_flags = address_space_get_flags(&receiver.address_space, read_address);
    check((read_flags & VM_FLAG_USER) != 0);
    check((read_flags & VM_FLAG_WRITABLE) == 0);
    check((read_flags & VM_FLAG_NO_EXECUTE) != 0);
    check(address_space_buffer_accessible(&receiver.address_space,
                                          read_address, 4100, 0));
    check(!address_space_buffer_accessible(&receiver.address_space,
                                           read_address, 4100, 1));
    check((int64_t)process_surface_map(&receiver, read_handle,
                                      OS_SURFACE_MAP_WRITE) ==
          SYS_ERR_PERMISSION_DENIED);
    check(kernel_object_close_handle(&owner.handle_table, surface, 0));
    check(process_surface_unmap_all(&receiver) == 0);
    check(receiver.active_surface_mapping_count == 0);
    check(kernel_object_close_handle(&receiver.handle_table, read_handle, 0));
    check(host_mm_allocated_pages() == 0);
    check(host_mm_mapped_pages() == 0);

    Process exhausted = {};
    init_process(&exhausted, 30);
    surface = kernel_graphics_surface_create(
        &owner.handle_table, owner.pid, 16, 16, OS64_PIXEL_FORMAT_RGB,
        OS_SURFACE_APPLICATION_RIGHTS);
    check(surface != 0);
    check(kernel_handle_resolve_copy(&owner.handle_table, surface,
                                     KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                     KERNEL_HANDLE_RIGHT_TRANSFER,
                                     &source));
    for (uint32_t i = 0; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        check(kernel_handle_alloc(&exhausted.handle_table,
                                  KERNEL_HANDLE_TYPE_VFS_FILE,
                                  KERNEL_HANDLE_RIGHT_READ,
                                  i + 1u, 0) != 0);
    }
    KernelGraphicsSurfaceInfo before_info;
    KernelGraphicsSurfaceInfo after_info;
    check(kernel_graphics_surface_get_info(source.object, &before_info) ==
          KERNEL_OBJECT_OK);
    check(kernel_object_clone_handle_with_rights(
              &exhausted.handle_table, &source, OS_SURFACE_TRANSFER_RIGHTS) == 0);
    check(kernel_graphics_surface_get_info(source.object, &after_info) ==
          KERNEL_OBJECT_OK && after_info.ref_count == before_info.ref_count);
    check(kernel_object_close_handle(&owner.handle_table, surface, 0));

    Process region_full = {};
    init_process(&region_full, 40);
    surface = kernel_graphics_surface_create(
        &owner.handle_table, owner.pid, 16, 16, OS64_PIXEL_FORMAT_RGB,
        OS_SURFACE_APPLICATION_RIGHTS);
    check(kernel_handle_resolve_copy(&owner.handle_table, surface,
                                     KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                     KERNEL_HANDLE_RIGHT_TRANSFER,
                                     &source));
    read_handle = kernel_object_clone_handle_with_rights(
        &region_full.handle_table, &source, OS_SURFACE_TRANSFER_RIGHTS);
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        check(address_space_add_region(&region_full.address_space,
                                       0x20000000u + (uint64_t)i * 0x2000u,
                                       VM_PAGE_SIZE,
                                       ADDRESS_SPACE_REGION_READ));
    }
    kernel_mappings = host_mm_mapped_pages();
    check((int64_t)process_surface_map(&region_full, read_handle,
                                      OS_SURFACE_MAP_READ) ==
          SYS_ERR_NO_RESOURCES);
    check(region_full.active_surface_mapping_count == 0);
    check(host_mm_mapped_pages() == kernel_mappings);
    check(kernel_object_close_handle(&owner.handle_table, surface, 0));
    check(kernel_object_close_handle(&region_full.handle_table, read_handle, 0));
    check(host_mm_allocated_pages() == 0);
    check(host_mm_mapped_pages() == 0);

    surface = kernel_graphics_surface_create(
        &owner.handle_table, owner.pid, 1025, 1, OS64_PIXEL_FORMAT_BGR,
        OS_SURFACE_APPLICATION_RIGHTS);
    check(surface != 0);
    kernel_mappings = host_mm_mapped_pages();
    host_mm_fail_map_after(1);
    check((int64_t)process_surface_map(
              &owner, surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE) ==
          SYS_ERR_OUT_OF_MEMORY);
    check(owner.active_surface_mapping_count == 0);
    check(owner.address_space.region_count == 0);
    check(host_mm_mapped_pages() == kernel_mappings);
    check(kernel_object_close_handle(&owner.handle_table, surface, 0));
    check(host_mm_allocated_pages() == 0);
    check(host_mm_mapped_pages() == 0);
    return failures == 0 ? 0 : 1;
}
"""

STUB_SOURCE = r"""
#include <stddef.h>
#include <stdlib.h>
extern "C" void* kmalloc(size_t size) { return malloc(size); }
extern "C" void kfree(void* pointer) { free(pointer); }
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_surface_mapping_") as temp_dir:
        temp = Path(temp_dir)
        source = temp / "surface_mapping_test.cpp"
        stubs = temp / "surface_mapping_stubs.cpp"
        binary = temp / "surface_mapping_test"
        source.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        stubs.write_text(textwrap.dedent(STUB_SOURCE), encoding="utf-8")
        command = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(REPO_ROOT / "include"),
            "-I", str(REPO_ROOT / "tools/fixtures"),
            str(REPO_ROOT / "kernel/sync/spinlock.cpp"),
            str(REPO_ROOT / "kernel/debug/fault_injection.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_objects.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(REPO_ROOT / "kernel/graphics/surface_backing.cpp"),
            str(REPO_ROOT / "kernel/mm/address_space.cpp"),
            str(REPO_ROOT / "kernel/process/process_surface.cpp"),
            str(REPO_ROOT / "tools/fixtures/kernel_mm_host_stubs.cpp"),
            str(source), str(stubs), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print("surface mapping test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
