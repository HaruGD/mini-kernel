#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

COPY_SOURCE = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/mm/address_space.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall/user_memory.h"

static Process process;
static int deny_all = 0;
static int deny_writes = 0;
static int gate_ready = 1;
static uint64_t inaccessible_at = UINT64_MAX;
static uint32_t active_leases = 0;
static int failures = 0;

static void check_at(int condition, int line) {
    if (!condition) {
        failures++;
        fprintf(stderr, "copy-boundary check failed at line %d\n", line);
    }
}
#define check(condition) check_at((condition), __LINE__)

Process* current_process() { return &process; }
uint32_t kernel_spinlock_depth() { return 0; }
int kernel_in_tlb_wait() { return 0; }
uint64_t address_space_identity(const AddressSpace*) { return 7; }
uint64_t address_space_tlb_generation(const AddressSpace*) { return 11; }
int address_space_user_access_begin(AddressSpace*, uint64_t identity) {
    if (!gate_ready || identity != 7) return 0;
    active_leases++;
    return 1;
}
void address_space_user_access_end(AddressSpace*) {
    if (active_leases != 0) active_leases--;
}
int address_space_buffer_accessible(const AddressSpace*, uint64_t start,
                                    uint64_t size, int writable) {
    return !deny_all && start != 0 && size != 0 && !(writable && deny_writes) &&
           start < inaccessible_at && size - 1u <= inaccessible_at - start - 1u;
}

struct VersionedRequest {
    uint32_t size;
    uint32_t version;
    uint64_t value;
};

int main() {
    process = {};
    process.active = 1;

    uint64_t value = 123;
    check(user_checked_add_u64(UINT64_MAX, 1, &value) == OS_ERR_OVERFLOW);
    check(value == 0);
    check(user_checked_add_u64(40, 2, &value) == OS_SUCCESS && value == 42);
    check(user_checked_multiply_u64(UINT64_MAX, 2, &value) == OS_ERR_OVERFLOW);
    check(value == 0);
    check(user_checked_multiply_u64(6, 7, &value) == OS_SUCCESS && value == 42);
    check(user_address_is_canonical(0x00007FFFFFFFFFFFULL));
    check(!user_address_is_canonical(0x0000800000000000ULL));

    uint8_t* pages = (uint8_t*)aligned_alloc(4096, 8192);
    check(pages != 0);
    for (uint32_t i = 0; i < 8192; i++) pages[i] = (uint8_t)(i & 0xFFu);
    uint8_t copied[32] = {};
    check(user_memory_copy_in(&process, copied,
                              (uint64_t)(uintptr_t)(pages + 4090),
                              sizeof(copied), 1, 0) == OS_SUCCESS);
    for (uint32_t i = 0; i < sizeof(copied); i++) {
        check(copied[i] == pages[4090 + i]);
    }
    check(active_leases == 0);

    uint8_t unchanged[8];
    for (uint32_t i = 0; i < sizeof(unchanged); i++) unchanged[i] = 0xA5u;
    deny_all = 1;
    check(user_memory_copy_in(&process, unchanged,
                              (uint64_t)(uintptr_t)pages,
                              sizeof(unchanged), 1, 0) == OS_ERR_BAD_BUFFER);
    for (uint32_t i = 0; i < sizeof(unchanged); i++) check(unchanged[i] == 0xA5u);
    deny_all = 0;

    uint8_t output[8] = {};
    const uint8_t source[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    deny_writes = 1;
    check(user_memory_copy_out(&process, (uint64_t)(uintptr_t)output,
                               source, sizeof(source), 1, 0) == OS_ERR_BAD_BUFFER);
    for (uint32_t i = 0; i < sizeof(output); i++) check(output[i] == 0);
    deny_writes = 0;
    check(user_memory_copy_out(&process, (uint64_t)(uintptr_t)output,
                               source, sizeof(source), 1, 0) == OS_SUCCESS);
    for (uint32_t i = 0; i < sizeof(output); i++) check(output[i] == source[i]);

    check(user_memory_copy_in(&process, copied, UINT64_MAX - 3u,
                              8, 1, 0) == OS_ERR_OVERFLOW);
    check(user_memory_copy_in(&process, copied, 0x0000800000000000ULL,
                              8, 1, 0) == OS_ERR_BAD_BUFFER);
    check(user_memory_copy_in(&process, copied,
                              (uint64_t)(uintptr_t)(pages + 1),
                              8, 8, 0) == OS_ERR_BAD_BUFFER);
    check(user_memory_copy_in(&process, copied, 0, 0, 1,
                              USER_MEMORY_ALLOW_EMPTY) == OS_SUCCESS);
    check(user_memory_copy_in(&process, copied, 0, 0, 1, 0) ==
          OS_ERR_INVALID_ARGUMENT);

    char* crossing = (char*)(pages + 4094);
    crossing[0] = 'o'; crossing[1] = 'k'; crossing[2] = '\0';
    char text[8] = {};
    uint64_t length = 99;
    check(user_memory_copy_cstring(&process,
                                   (uint64_t)(uintptr_t)crossing,
                                   text, sizeof(text), &length) == OS_SUCCESS);
    check(length == 2 && text[0] == 'o' && text[1] == 'k' && text[2] == '\0');
    crossing[0] = 'x'; crossing[1] = '\0';
    inaccessible_at = (uint64_t)(uintptr_t)(pages + 4096);
    check(user_memory_copy_cstring(&process,
                                   (uint64_t)(uintptr_t)crossing,
                                   text, sizeof(text), &length) == OS_SUCCESS);
    check(length == 1 && text[0] == 'x' && text[1] == '\0');
    inaccessible_at = UINT64_MAX;
    for (uint32_t i = 0; i < sizeof(text); i++) text[i] = 'x';
    for (uint32_t i = 0; i < 8; i++) pages[i] = 'a';
    length = 99;
    check(user_memory_copy_cstring(&process, (uint64_t)(uintptr_t)pages,
                                   text, sizeof(text), &length) ==
          OS_ERR_BUFFER_TOO_SMALL);
    check(length == 0);

    VersionedRequest request = {sizeof(VersionedRequest), 3, 0xAABBCCDDu};
    VersionedRequest snapshot = {};
    check(user_memory_copy_versioned_struct_in(
              &process, (uint64_t)(uintptr_t)&request, &snapshot,
              sizeof(snapshot), sizeof(snapshot), 3, 8) == OS_SUCCESS);
    check(snapshot.value == request.value);
    request.version = 4;
    snapshot.value = 77;
    check(user_memory_copy_versioned_struct_in(
              &process, (uint64_t)(uintptr_t)&request, &snapshot,
              sizeof(snapshot), sizeof(snapshot), 3, 8) ==
          OS_ERR_INVALID_ARGUMENT);
    check(snapshot.value == 77);

    const uint8_t valid_utf8[] = {'A', 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80};
    const uint8_t invalid_utf8[] = {0xED, 0xA0, 0x80};
    check(user_memory_validate_utf8(valid_utf8, sizeof(valid_utf8)) == OS_SUCCESS);
    check(user_memory_validate_utf8(invalid_utf8, sizeof(invalid_utf8)) ==
          OS_ERR_INVALID_ARGUMENT);

    gate_ready = 0;
    check(user_memory_copy_in(&process, copied,
                              (uint64_t)(uintptr_t)pages,
                              sizeof(copied), 1, 0) == OS_ERR_NOT_READY);
    free(pages);
    check(active_leases == 0);
    return failures == 0 ? 0 : 1;
}
"""

RACE_SOURCE = r"""
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <thread>

#include "kernel/mm/address_space.h"
#include "kernel_mm_host_stubs.h"

static int failures = 0;
static void check_at(int condition, int line) {
    if (!condition) {
        failures++;
        fprintf(stderr, "unmap-race check failed at line %d\n", line);
    }
}
#define check(condition) check_at((condition), __LINE__)

int main() {
    host_mm_reset();
    AddressSpace space = {};
    address_space_init(&space);
    uint32_t pages = 0;
    const uint64_t address = 0x20000000u;
    check(address_space_alloc_map_range(
              &space, address, VM_PAGE_SIZE,
              VM_FLAG_USER | VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE,
              &pages));
    check(pages == 1);
    check(address_space_user_access_begin(&space,
                                          address_space_identity(&space)));
    check(space.active_user_accesses == 1);

    std::atomic<int> finished{0};
    std::thread unmapper([&]() {
        kernel_host_set_interrupts_enabled(1);
        const uint32_t removed =
            address_space_unmap_free_range(&space, address, 1);
        finished.store(removed == 1 ? 1 : -1, std::memory_order_release);
    });
    for (uint32_t spin = 0; spin < 1000000 && space.shootdown_active == 0; spin++) {
        __asm__ volatile("pause");
    }
    check(space.shootdown_active == 1);
    check(finished.load(std::memory_order_acquire) == 0);
    check(address_space_get_phys(&space, address) != 0);
    address_space_user_access_end(&space);
    unmapper.join();
    check(finished.load(std::memory_order_acquire) == 1);
    check(space.active_user_accesses == 0);
    check(address_space_get_phys(&space, address) == 0);
    check(host_mm_allocated_pages() == 0);
    return failures == 0 ? 0 : 1;
}
"""


def compile_and_run(source_text: str, sources: list[Path], name: str,
                    extra: list[str] | None = None) -> None:
    with tempfile.TemporaryDirectory(prefix=f"os64_{name}_") as temp_dir:
        temp = Path(temp_dir)
        source = temp / f"{name}.cpp"
        binary = temp / name
        source.write_text(textwrap.dedent(source_text), encoding="utf-8")
        command = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            "-I", str(ROOT / "tools/fixtures"),
        ]
        command.extend(extra or [])
        command.extend(str(path) for path in sources)
        command.extend([str(source), "-o", str(binary)])
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)


def main() -> int:
    compile_and_run(
        COPY_SOURCE,
        [ROOT / "kernel/syscall/user_memory.cpp"],
        "user_memory_copy_test",
    )
    compile_and_run(
        RACE_SOURCE,
        [
            ROOT / "kernel/sync/spinlock.cpp",
            ROOT / "kernel/debug/fault_injection.cpp",
            ROOT / "kernel/mm/address_space.cpp",
            ROOT / "tools/fixtures/kernel_mm_host_stubs.cpp",
        ],
        "user_memory_unmap_race_test",
        ["-pthread"],
    )
    print("user memory validation test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
