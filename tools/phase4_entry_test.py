#!/usr/bin/env python3
from pathlib import Path


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures: list[str] = []
    roadmap = (root / "docs/roadmap.md").read_text(encoding="utf-8")
    baseline = (root / "docs/phases/phase-4/entry_baseline.md").read_text(encoding="utf-8")
    contracts = (root / "docs/phases/phase-4/compositor_contracts.md").read_text(encoding="utf-8")
    kernel_main = (root / "kernel/core/kernel64_main.cpp").read_text(encoding="utf-8")
    display_owner = (root / "kernel/graphics/display_owner.cpp").read_text(encoding="utf-8")
    makefile = (root / "Makefile").read_text(encoding="utf-8")

    require("BootInfo v3" in roadmap, "roadmap does not name BootInfo v3", failures)
    phase36 = roadmap.split("## Phase 3.6:", 1)[-1].split("## Phase 4:", 1)[0]
    require("- [ ]" not in phase36, "roadmap still marks Phase 3.6 incomplete", failures)
    require("- [ ]" not in baseline.split("## Preflight Gates", 1)[-1],
            "Phase 4 baseline has an open preflight gate", failures)

    for term in ("windowd_c.elf", "displayd_c.elf", "inputd_c.elf",
                 "os_surface_create", "maximum windows: 12", "Failure And Cleanup"):
        require(term in contracts, f"Phase 4 contract missing {term}", failures)

    ata = kernel_main.find("ata.init();")
    keyboard = kernel_main.find("keyboard.init();")
    pit = kernel_main.find("pit.init();")
    activate = kernel_main.find("driver_manager_activate_linked_kernel();")
    interrupts = kernel_main.find('__asm__ volatile("sti")')
    require(0 <= ata < activate and 0 <= keyboard < activate and 0 <= pit < activate < interrupts,
            "linked kernel ready transition precedes hardware initialization", failures)

    begin = display_owner.split("void display_owner_begin", 1)[-1].split("void display_owner_end", 1)[0]
    require("uint64_t flags = irq_save();" in begin and "irq_restore(flags);" in begin,
            "display ownership begin does not bound its IRQ-off transition", failures)
    require("token->flags = irq_save()" not in display_owner,
            "display ownership still carries IRQ-off state across drawing", failures)
    require("test-phase4-entry: test-driver-regression test-graphics-contracts test-concurrency" in makefile,
            "Phase 4 entry target is not wired", failures)

    if failures:
        print("Phase 4 entry test failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("Phase 4 entry test OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
