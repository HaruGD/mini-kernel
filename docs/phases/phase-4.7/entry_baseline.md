# Phase 4.7 Entry Baseline

This document records the driver-memory state from which Phase 4.7 begins. It
is a baseline, not an implementation claim.

## Entry Gate

Phase 4.6 closed on 2026-07-30.

- Phase 4.6 implementation and closure commits include `13fce61` and
  `61a13bb`;
- closure evidence commit: `198a298`;
- clean normal and diagnostic UEFI build: PASS;
- `make test-phase46`: PASS;
- `make test-closure`: PASS;
- required four-vCPU 60-second soak: 12 cycles, 48 thread programs, and 48
  GUI windows;
- warmed/final resource tuple:
  `(4,21,1,0,4,0,1,26912,1944432,1961984)` with zero drift.

The optional one-hour release soak is not an entry requirement.

## Current Driver Image Memory

The `.drv` loader already:

- validates section shapes, sizes, alignments, symbols, imports, exports,
  relocations, manifest permissions, dependencies, and local-test signatures;
- allocates page-backed `CODE`, `RODATA`, `DATA`, and `BSS` sections;
- relocates and resolves imports while the image is writable;
- applies final `CODE=RX`, `RODATA=R/NX`, and `DATA/BSS=RW/NX` permissions;
- frees mapped section pages on load rollback and successful unload.

Loaded sections use the dedicated range
`0x70000000-0x78000000`. Allocation advances one global
`g_driver_section_next_virtual` cursor. Unload returns physical pages but does
not return the virtual interval to a reusable allocator. Repeated
load/unload can therefore exhaust the 128 MiB arena without a physical-page
leak.

## Current General Allocation Model

Every driver can import untagged kernel `kmalloc(size)` and `kfree(pointer)`.

- allocations are not associated with a driver generation or bound device;
- there are no driver quotas, tags, leak records, or automatic unload cleanup;
- there is no sleepable versus atomic allocation contract;
- there is no dedicated page, alignment, physical-contiguity, or DMA-safe
  allocation API;
- a raw pointer is the only allocation identity.

The existing heap remains valid kernel infrastructure. Phase 4.7 adds a
driver-facing ownership layer rather than replacing the heap.

## Current PCI And MMIO Model

PCI discovery, BAR decoding, device binding, memory-space enable, and
bus-master enable exist. A driver with `PCI|MMIO` permission may map a BAR.

PCI MMIO mappings use the dedicated range
`0x60000000-0x70000000` with writable, write-through, cache-disabled, NX
pages. This range also uses one monotonic cursor and has no unmap/reuse path.

The exported accessors accept raw virtual addresses:

```text
mmio_read32(address)
mmio_write32(address, value)
```

The permission check controls whether a driver may import the function. It
does not prove that the address belongs to a BAR bound to that driver, lies
within the mapped length, has valid alignment, or is still live.

## Current DMA And Isolation Model

There is no driver DMA API.

- CPU virtual, physical, and device-visible addresses are not separate ABI
  types;
- there is no DMA mask negotiation;
- there are no coherent or streaming mappings;
- there is no scatter/gather list, bounce buffer, or cache synchronization;
- there is no DMA mapping ownership or teardown;
- there is no IOMMU domain or device attachment policy.

Bus mastering can currently be enabled without establishing a tracked DMA
domain. This is acceptable only for the existing prototype and must not be
used as the foundation for new production device drivers.

## Current Unload Model

Unload already rejects built-ins and ready dependents, invokes the driver exit
function, unregisters module exports and IRQ hooks, removes PCI bindings,
frees image sections, and removes the driver record.

It does not yet:

- transition through an explicit quiescing state;
- prevent new imported calls while unload begins;
- wait for in-flight IRQ, callback, worker, or exported-function execution;
- disable device bus mastering before memory teardown;
- revoke MMIO and DMA mappings;
- report or reclaim driver-owned heap allocations;
- prove that no CPU can return into unloaded code.

## Inherited Invariants

Phase 4.7 must preserve:

- Phase 3.6 package policy and all seven allowed artifact/stage/load-policy
  combinations;
- code/data W^X and malformed-package rejection;
- dependency-safe activation and unload denial;
- Phase 4 GUI and service recovery;
- Phase 4.5 thread lifetime and synchronization;
- Phase 4.6 SMP, lock ordering, interrupt ownership, TLB shootdown, and
  one-vCPU fallback;
- identical warmed/final resource accounting in required soak workloads.

## Entry Decision

The entry evidence is present. Phase 4.7A may begin. Phase 5 may proceed in
parallel, but AHCI/NVMe, xHCI, networking, audio, native GPU, and device
passthrough work remain gated on the relevant Phase 4.7 DMA and ownership
contracts.
