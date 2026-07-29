#!/usr/bin/env python3
import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <stdint.h>

#include "kernel/mm/address_space.h"
#include "kernel/mm/pmm.h"
#include "kernel/spinlock.h"
#include "kernel_mm_host_stubs.h"

static int failures = 0;
static void check(int condition) { if (!condition) failures++; }

int main() {
    host_mm_reset();
    kernel_spinlock_reset_stats();
    kernel_host_set_interrupts_enabled(1);

    KernelSpinlock process_lock;
    kernel_spinlock_init(&process_lock, KERNEL_LOCK_CLASS_PROCESS, "process");
    KernelSpinlockToken process_token;
    check(kernel_spinlock_acquire(&process_lock, &process_token));
    check(!kernel_tlb_wait_enter());
    kernel_spinlock_release(&process_lock, &process_token);

    kernel_host_set_interrupts_enabled(0);
    check(!kernel_tlb_wait_enter());
    kernel_host_set_interrupts_enabled(1);
    check(kernel_tlb_wait_enter());
    check(kernel_in_tlb_wait());
    check(kernel_preemption_disable_depth() == 1);
    KernelSpinlockToken denied;
    check(!kernel_spinlock_acquire(&process_lock, &denied));
    kernel_tlb_wait_leave();
    check(!kernel_in_tlb_wait());
    check(kernel_preemption_disable_depth() == 0);

    AddressSpace space = {};
    address_space_init(&space);
    uint64_t old_identity = address_space_identity(&space);
    uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_block();
    check(phys != 0);
    check(address_space_map_page(&space,
                                 0x20000000u,
                                 phys,
                                 VM_FLAG_USER | VM_FLAG_WRITABLE |
                                     VM_FLAG_NO_EXECUTE));
    uint64_t mapped_generation = address_space_tlb_generation(&space);
    space.cached_cpu_mask = 0x0Fu;
    check(address_space_unmap_free_range(&space, 0x20000000u, 1) == 1);
    check(host_tlb_last_target_mask() == 0x0Fu);
    check(host_tlb_last_generation() > mapped_generation);
    check(host_tlb_last_token() != 0);
    check(space.quarantined_page_count == 0);
    check(space.retired_page_count == 1);
    check(host_mm_allocated_pages() == 0);

    phys = (uint64_t)(uintptr_t)pmm_alloc_block();
    check(phys != 0);
    check(address_space_map_page(&space,
                                 0x20001000u,
                                 phys,
                                 VM_FLAG_USER | VM_FLAG_WRITABLE |
                                     VM_FLAG_NO_EXECUTE));
    space.cached_cpu_mask = 0x03u;
    host_tlb_fail_next();
    check(address_space_unmap_free_range(&space, 0x20001000u, 1) == 1);
    check(space.shootdown_timeout_count == 1);
    check(space.quarantined_page_count == 1);
    check(space.retired_page_count == 1);
    check(host_mm_allocated_pages() == 1);

    address_space_recycle(&space);
    check(address_space_identity(&space) != old_identity);
    check(address_space_tlb_generation(&space) == 1);
    check(space.active_cpu_mask == 0);
    check(space.cached_cpu_mask == 0);

    KernelSpinlockStats stats;
    kernel_spinlock_get_stats(&stats);
    check(stats.tlb_wait_violations == 2);
    check(stats.tlb_wait_entries == 1);
    check(stats.order_violations == 1);
    host_mm_reset();
    return failures == 0 ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64_tlb_order_") as temporary:
        temp = Path(temporary)
        source = temp / "tlb_order.cpp"
        binary = temp / "tlb_order"
        source.write_text(textwrap.dedent(SOURCE), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-DOS64_HOST_TEST", "-I", str(ROOT / "include"),
            "-I", str(ROOT / "tools/fixtures"),
            str(ROOT / "kernel/sync/spinlock.cpp"),
            str(ROOT / "kernel/debug/fault_injection.cpp"),
            str(ROOT / "tools/fixtures/kernel_mm_host_stubs.cpp"),
            str(ROOT / "kernel/mm/address_space.cpp"),
            str(source), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("TLB lock-order and quarantine model OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
