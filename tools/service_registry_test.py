#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


TEST_SOURCE = r"""
#include <stdint.h>
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"

static int failures = 0;

static void check(int condition) {
    if (!condition) {
        failures++;
    }
}

static void init_process(Process* process, uint32_t pid) {
    process_clear(process);
    process->pid = pid;
    process->active = 1;
    process->state = PROCESS_STATE_RUNNING;
}

static void clear_all() {
    service_registry_init();
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
}

int main() {
    clear_all();
    Process* owner = &process_table[0];
    Process* other = &process_table[1];
    init_process(owner, 100);
    init_process(other, 200);

    check(service_registry_capacity() == SERVICE_REGISTRY_CAPACITY);
    check(service_registry_count() == 0);
    check(service_name_valid("display") == 1);
    check(service_name_valid("input0") == 1);
    check(service_name_valid("window-manager") == 1);
    check(service_name_valid("") == 0);
    check(service_name_valid("Display") == 0);
    check(service_name_valid("1display") == 0);
    check(service_name_valid("service_name_too_long") == 0);
    check(service_register(0, "display", OS_SERVICE_FLAG_NONE) == SERVICE_ERR_NOT_READY);
    check(service_register(owner, "Display", OS_SERVICE_FLAG_NONE) == SERVICE_ERR_INVALID_ARGUMENT);
    check(service_register(owner, "display", 0x80000000u) == SERVICE_ERR_INVALID_ARGUMENT);

    check(service_register(owner, "display", OS_SERVICE_FLAG_NONE) == SERVICE_OK);
    check(service_registry_count() == 1);
    check(service_register(owner, "display", OS_SERVICE_FLAG_NONE) == SERVICE_ERR_ALREADY_EXISTS);
    check(service_register(other, "display", OS_SERVICE_FLAG_NONE) == SERVICE_ERR_ALREADY_EXISTS);

    OsServiceInfo info;
    check(service_find("display", &info) == SERVICE_OK);
    check(info.size == sizeof(OsServiceInfo));
    check(info.owner_pid == owner->pid);
    check(info.state == OS_SERVICE_STATE_REGISTERED);
    check(info.flags == OS_SERVICE_FLAG_NONE);
    check(info.generation != 0);
    uint32_t first_generation = info.generation;
    check(service_find("missing", &info) == SERVICE_ERR_NOT_FOUND);
    check(service_find("Display", &info) == SERVICE_ERR_INVALID_ARGUMENT);
    check(service_find("display", 0) == SERVICE_ERR_BAD_BUFFER);

    check(service_unregister(other, "display") == SERVICE_ERR_PERMISSION_DENIED);
    check(service_unregister(owner, "display") == SERVICE_OK);
    check(service_registry_count() == 0);
    check(service_find("display", &info) == SERVICE_ERR_NOT_FOUND);
    check(service_unregister(owner, "display") == SERVICE_ERR_NOT_FOUND);

    check(service_register(owner, "input", OS_SERVICE_FLAG_SYSTEM) == SERVICE_OK);
    check(service_find("input", &info) == SERVICE_OK);
    check(info.flags == OS_SERVICE_FLAG_SYSTEM);
    process_mark_returned(owner, PROCESS_TERM_EXIT, 0);
    check(service_registry_count() == 0);
    check(service_find("input", &info) == SERVICE_ERR_NOT_FOUND);

    init_process(owner, 101);
    check(service_register(owner, "input", OS_SERVICE_FLAG_NONE) == SERVICE_OK);
    check(service_find("input", &info) == SERVICE_OK);
    check(info.generation != first_generation);
    process_mark_failed(owner, PROCESS_TERM_KILLED, 1);
    check(service_find("input", &info) == SERVICE_ERR_NOT_FOUND);

    clear_all();
    init_process(owner, 300);
    char name[OS_SERVICE_NAME_MAX];
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        name[0] = 's';
        name[1] = (char)('0' + (i / 10));
        name[2] = (char)('0' + (i % 10));
        name[3] = '\0';
        check(service_register(owner, name, OS_SERVICE_FLAG_NONE) == SERVICE_OK);
    }
    check(service_registry_count() == SERVICE_REGISTRY_CAPACITY);
    check(service_register(owner, "extra", OS_SERVICE_FLAG_NONE) == SERVICE_ERR_NO_RESOURCES);

    uint32_t listed = 0;
    for (uint32_t i = 0; i < SERVICE_REGISTRY_CAPACITY; i++) {
        if (service_registry_get_info(i, &info) == SERVICE_OK) {
            listed++;
            check(info.owner_pid == owner->pid);
            check(info.state == OS_SERVICE_STATE_REGISTERED);
        }
    }
    check(listed == SERVICE_REGISTRY_CAPACITY);
    check(service_registry_get_info(SERVICE_REGISTRY_CAPACITY, &info) == SERVICE_ERR_INVALID_ARGUMENT);

    service_unregister_owner(owner->pid);
    check(service_registry_count() == 0);
    clear_all();
    return failures == 0 ? 0 : 1;
}
"""


STUB_SOURCE = r"""
#include <stdint.h>

uint32_t vfs_close_all_for_owner(uint32_t) {
    return 0;
}

void copy_string64(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_service_registry_") as temp_dir:
        temp_path = Path(temp_dir)
        source_path = temp_path / "service_registry_test.cpp"
        stub_path = temp_path / "service_registry_stubs.cpp"
        binary_path = temp_path / "service_registry_test"
        source_path.write_text(textwrap.dedent(TEST_SOURCE), encoding="utf-8")
        stub_path.write_text(textwrap.dedent(STUB_SOURCE), encoding="utf-8")

        compile_cmd = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO_ROOT / "include"),
            str(REPO_ROOT / "kernel/handle/kernel_handle.cpp"),
            str(REPO_ROOT / "kernel/ipc/ipc_mailbox.cpp"),
            str(REPO_ROOT / "kernel/input/input_event_queue.cpp"),
            str(REPO_ROOT / "kernel/process/process64.cpp"),
            str(REPO_ROOT / "kernel/service/service_registry.cpp"),
            str(source_path),
            str(stub_path),
            "-o",
            str(binary_path),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_path)], check=True)

    print("service registry test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
