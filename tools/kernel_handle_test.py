#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/handle/kernel_handle.h"
#include "kernel/handle/kernel_objects.h"
#include "os64/graphics_types.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

int main() {
    KernelHandleTable table;
    kernel_handle_table_init(&table);
    check(kernel_handle_count_type(&table, KERNEL_HANDLE_TYPE_NONE) == 0);

    KernelHandleTable receiver;
    kernel_handle_table_init(&receiver);
    kernel_objects_init();

    uint64_t shared = kernel_shared_memory_create(&table,
                                                  42,
                                                  128,
                                                  KERNEL_HANDLE_RIGHT_READ |
                                                  KERNEL_HANDLE_RIGHT_WRITE |
                                                  KERNEL_HANDLE_RIGHT_MAP |
                                                  KERNEL_HANDLE_RIGHT_TRANSFER);
    check(shared != 0);
    KernelHandle* shared_handle = kernel_handle_resolve(&table,
                                                        shared,
                                                        KERNEL_HANDLE_TYPE_SHARED_MEMORY,
                                                        KERNEL_HANDLE_RIGHT_MAP);
    check(shared_handle != 0);
    uint8_t bytes[4] = {1, 2, 3, 4};
    uint8_t readback[4] = {0, 0, 0, 0};
    check(shared_handle != 0 &&
          kernel_shared_memory_write(shared_handle->object, 8, bytes, sizeof(bytes)) == KERNEL_OBJECT_OK);
    uint64_t shared_clone = kernel_object_clone_handle(&receiver, shared_handle);
    check(shared_clone != 0);
    KernelHandle* receiver_shared = kernel_handle_resolve(&receiver,
                                                          shared_clone,
                                                          KERNEL_HANDLE_TYPE_SHARED_MEMORY,
                                                          KERNEL_HANDLE_RIGHT_READ);
    check(receiver_shared != 0);
    check(receiver_shared != 0 &&
          kernel_shared_memory_read(receiver_shared->object, 8, readback, sizeof(readback)) == KERNEL_OBJECT_OK);
    check(readback[0] == 1 && readback[1] == 2 && readback[2] == 3 && readback[3] == 4);

    KernelSharedMemoryInfo shared_info;
    check(shared_handle != 0 &&
          kernel_shared_memory_get_info(shared_handle->object, &shared_info) == KERNEL_OBJECT_OK &&
          shared_info.ref_count == 2 &&
          shared_info.page_count == 1);
    check(kernel_object_close_handle(&table, shared, 0) == 1);
    check(receiver_shared != 0 &&
          kernel_shared_memory_get_info(receiver_shared->object, &shared_info) == KERNEL_OBJECT_OK &&
          shared_info.ref_count == 1);
    check(kernel_object_release_table(&receiver) == 1);
    check(receiver_shared != 0 &&
          kernel_shared_memory_get_info(receiver_shared->object, &shared_info) == KERNEL_OBJECT_ERR_NOT_FOUND);

    uint64_t surface = kernel_graphics_surface_create(&table,
                                                      42,
                                                      32,
                                                      16,
                                                      OS64_PIXEL_FORMAT_RGB,
                                                      KERNEL_HANDLE_RIGHT_READ |
                                                      KERNEL_HANDLE_RIGHT_WRITE |
                                                      KERNEL_HANDLE_RIGHT_MAP |
                                                      KERNEL_HANDLE_RIGHT_TRANSFER);
    check(surface != 0);
    KernelHandle* surface_handle = kernel_handle_resolve(&table,
                                                         surface,
                                                         KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                                         KERNEL_HANDLE_RIGHT_MAP);
    KernelGraphicsSurfaceInfo surface_info;
    check(surface_handle != 0 &&
          kernel_graphics_surface_get_info(surface_handle->object, &surface_info) == KERNEL_OBJECT_OK &&
          surface_info.width == 32 &&
          surface_info.height == 16 &&
          surface_info.stride_pixels == 32 &&
          surface_info.ref_count == 1);
    check(surface_handle != 0 && kernel_graphics_surface_get(surface_handle->object) != 0);
    check(kernel_object_close_handle(&table, surface, 0) == 1);
    check(surface_handle != 0 &&
          kernel_graphics_surface_get_info(surface_handle->object, &surface_info) == KERNEL_OBJECT_ERR_NOT_FOUND);

    uint64_t file = kernel_handle_alloc(&table,
                                        KERNEL_HANDLE_TYPE_VFS_FILE,
                                        KERNEL_HANDLE_RIGHT_READ | KERNEL_HANDLE_RIGHT_SEEK,
                                        7,
                                        0);
    check(file != 0);
    check(kernel_handle_is_valid_token(file) == 1);
    check(kernel_handle_count_type(&table, KERNEL_HANDLE_TYPE_VFS_FILE) == 1);
    check(kernel_handle_resolve(&table, file, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_READ) != 0);
    check(kernel_handle_resolve(&table, file, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_WRITE) == 0);
    check(kernel_handle_resolve(&table, file, KERNEL_HANDLE_TYPE_VFS_DIR, 0) == 0);
    check(kernel_object_clone_handle(&receiver, kernel_handle_resolve(&table,
                                                                      file,
                                                                      KERNEL_HANDLE_TYPE_VFS_FILE,
                                                                      KERNEL_HANDLE_RIGHT_READ)) == 0);

    KernelHandle closed;
    check(kernel_handle_close(&table, file, &closed) == 1);
    check(closed.object == 7);
    check(kernel_handle_resolve(&table, file, KERNEL_HANDLE_TYPE_VFS_FILE, 0) == 0);
    check(kernel_handle_close(&table, file, 0) == 0);

    uint64_t next = kernel_handle_alloc(&table,
                                        KERNEL_HANDLE_TYPE_VFS_FILE,
                                        KERNEL_HANDLE_RIGHT_READ,
                                        8,
                                        0);
    check(next != 0);
    check(next != file);
    check(kernel_handle_resolve(&table, file, KERNEL_HANDLE_TYPE_VFS_FILE, 0) == 0);
    check(kernel_handle_resolve(&table, next, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_READ) != 0);

    uint32_t allocated = 1;
    for (uint32_t i = 1; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        uint64_t handle = kernel_handle_alloc(&table,
                                              KERNEL_HANDLE_TYPE_VFS_DIR,
                                              KERNEL_HANDLE_RIGHT_ENUMERATE,
                                              i,
                                              0);
        check(handle != 0);
        allocated++;
    }
    check(allocated == KERNEL_HANDLE_TABLE_SIZE);
    check(kernel_handle_alloc(&table, KERNEL_HANDLE_TYPE_VFS_DIR, 0, 1, 0) == 0);
    check(kernel_handle_close_all_type(&table, KERNEL_HANDLE_TYPE_VFS_DIR) == KERNEL_HANDLE_TABLE_SIZE - 1);
    check(kernel_handle_count_type(&table, KERNEL_HANDLE_TYPE_NONE) == 1);

    kernel_handle_table_init(&table);
    check(kernel_handle_count_type(&table, KERNEL_HANDLE_TYPE_NONE) == 0);
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_kernel_handle_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "kernel_handle_test.cpp"
        binary_path = temp_path / "kernel_handle_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        stub_path = temp_path / "kernel_handle_stubs.cpp"
        stub_path.write_text(
            textwrap.dedent(
                r"""
                #include <stddef.h>
                #include <stdlib.h>

                extern "C" void* kmalloc(size_t size) {
                    return malloc(size);
                }

                extern "C" void kfree(void* pointer) {
                    free(pointer);
                }
                """
            ),
            encoding="utf-8",
        )

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DOS64_HOST_TEST",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/sync/spinlock.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(REPO_ROOT / "kernel/handle/kernel_objects.cpp"),
            str(REPO_ROOT / "kernel/graphics/graphics_surface.cpp"),
            str(source_path),
            str(stub_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("kernel handle test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
