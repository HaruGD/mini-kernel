#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"missing {label}: {token}")


def require_order(text: str, tokens: list[str], label: str) -> None:
    positions = [text.find(token) for token in tokens]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise SystemExit(f"invalid {label} ordering: {tokens}")


def main() -> int:
    loader = (ROOT / "kernel/driver/driver_loader.cpp").read_text(encoding="utf-8")
    unload = (ROOT / "kernel/driver/driver_unload.cpp").read_text(encoding="utf-8")
    va_header = (ROOT / "include/kernel/driver/driver_va.h").read_text(encoding="utf-8")
    smp = (ROOT / "kernel/cpu/smp.cpp").read_text(encoding="utf-8")

    if "g_driver_section_next_virtual" in loader:
        raise SystemExit("monotonic driver image cursor is still present")
    require(va_header, "DRIVER_IMAGE_VA_GUARD_PAGES 1u", "guard-page policy")
    require(loader, "VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE", "initial RW/NX mapping")
    require(loader, "if (section->kind == DRV_SECTION_CODE)", "code protection")
    require(loader, "flags = 0;", "RX code flags")
    require(loader, "VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE", "RW/NX data flags")
    require(smp, "const int kernel_global = identity == 0 && root == 0;",
            "kernel-global TLB request")
    require_order(loader, ["vm_unmap_free_range_tlb_safe(base, section->page_count)",
                           "driver_resource_release(owner, section->resource",
                           "driver_image_va_release(owner, va)"],
                  "loader rollback")
    require_order(unload, ["vm_unmap_free_range_tlb_safe(",
                           "driver_resource_release(",
                           "driver_image_va_release(loaded->owner, va)"],
                  "unload")
    vm = (ROOT / "kernel/mm/vm.cpp").read_text(encoding="utf-8")
    require_order(vm, ["vm_unmap_page(address)",
                       "smp_kernel_tlb_shootdown(chunk_begin, unmapped)",
                       "pmm_free_block((void*)(uintptr_t)physical[page])"],
                  "deferred physical-page retirement")
    print("driver image W^X, guard, rollback, and TLB reuse contract OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
