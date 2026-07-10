#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/handle/kernel_handle.h"

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
                                              KERNEL_HANDLE_TYPE_SHARED_MEMORY,
                                              KERNEL_HANDLE_RIGHT_MAP,
                                              i,
                                              0);
        check(handle != 0);
        allocated++;
    }
    check(allocated == KERNEL_HANDLE_TABLE_SIZE);
    check(kernel_handle_alloc(&table, KERNEL_HANDLE_TYPE_VFS_DIR, 0, 1, 0) == 0);
    check(kernel_handle_close_all_type(&table, KERNEL_HANDLE_TYPE_SHARED_MEMORY) == KERNEL_HANDLE_TABLE_SIZE - 1);
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

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(source_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("kernel handle test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
