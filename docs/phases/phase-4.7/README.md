# Phase 4.7: Driver Memory And DMA Foundation

Phase 4.7 turns the current loadable-driver prototype into a bounded driver
runtime suitable for modern bus-mastering devices. It closes the gap between
page-protected `.drv` images and the memory, MMIO, DMA, ownership, and unload
contracts required by AHCI/NVMe, xHCI, networking, audio, and native GPU
drivers.

The phase is a parallel hardware-foundation track. Phase 5 desktop work may
proceed independently, but new bus-mastering hardware drivers must not bypass
the Phase 4.7 contracts.

## Documents

- [Entry baseline](entry_baseline.md): the exact Phase 4.6 and driver-runtime
  state from which implementation begins.
- [Implementation plan](implementation_plan.md): fixed ownership, allocation,
  MMIO, DMA, IOMMU-ready, and unload policies plus ordered subphases.
- [Regression matrix](regression_matrix.md): required positive, negative,
  QEMU, resource, and inherited coverage.
- [Progress ledger](progress.md): live implementation state and immutable
  evidence.

## Current Status

Phase 4.7A through 4.7H completed on 2026-08-01. In addition to generation
ownership, reusable image VA, and owned allocations, commits `e00cb72`,
`257526f`, `94202fe`, and `39e53f3` provide capability-scoped BAR mappings,
coherent DMA, streaming/scatter-gather mappings, trusted-direct domains, and
an actual QEMU EDU DMA round trip. Commit `1314b15` adds quiescent unload with
generation-owned execution pins, new-entry rejection, bus-master shutdown,
bounded drain, and timeout quarantine. Commit `1a4907f` closes the phase with
expanded deterministic failure injection, generic PCI INTx routing, checked
QEMU EDU IRQ/coherent/streaming/SG transfers, and a drift-free 60-second
four-vCPU driver/GUI churn soak.

The entry system already provides page-separated `.drv` images with
`CODE=RX`, `RODATA=R/NX`, and `DATA/BSS=RW/NX`, package validation,
dependency-aware unload denial, PCI binding, IRQ registration, and section
page release. It does not yet provide a remapping IOMMU backend. The present
direct DMA backend is explicitly trusted-only and never reports isolation.

## Intended Result

At Phase 4.7 closure:

- unloading and reloading drivers reuses virtual address space without
  leaking physical pages or stale mappings;
- every driver allocation, BAR mapping, DMA mapping, IRQ, and device binding
  has a generation-checked driver/device owner;
- interrupt and thread allocation contexts are explicit and enforced;
- MMIO access is constrained to a mapped BAR handle and checked offset;
- CPU virtual, physical, and device-visible DMA addresses are distinct types;
- coherent, streaming, scatter/gather, DMA-mask, cache-sync, and bounce-buffer
  paths have bounded APIs and cleanup;
- a remapping-capable IOMMU backend can attach without changing driver ABI,
  while absence of an IOMMU never creates a false isolation claim;
- unload first quiesces entry, IRQ, work, and DMA, then releases resources in
  dependency order before removing executable pages;
- fault injection, repeated load/use/unload, a QEMU DMA-capable test device,
  Phase 4.6, and full project closure all pass.

## Scope Boundary

Phase 4.7 includes the common driver-memory runtime and a diagnostic QEMU
device path. It does not implement a production AHCI/NVMe, xHCI, network,
audio, or GPU driver.

The first IOMMU work freezes domain and fallback policy. Full Intel VT-d and
AMD-Vi feature coverage, interrupt remapping, PCI passthrough, SR-IOV, ATS,
PRI, PASID, and nested translation remain later hardware/virtualization work.

Kernel-mode drivers continue to share the kernel address space, as they do in
many monolithic kernels. User-mode driver hosting is a later isolation option,
not an implicit Phase 4.7 requirement.
