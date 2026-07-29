# Phase 4.7 Implementation Plan

This plan introduces a bounded driver-resource runtime above the existing
kernel PMM, VM, heap, PCI, and `.drv` loader. The first goal is correctness,
ownership, and deterministic cleanup. Performance tuning and production
device drivers follow only after these contracts close.

Actual state belongs in [progress.md](progress.md). Required evidence belongs
in [regression_matrix.md](regression_matrix.md). Reserved target names are
plans until real Makefile targets exist and pass.

## Target Model

```text
DriverIdentity(slot, generation)
  -> lifecycle: LOADING -> ACTIVE -> QUIESCING -> DEAD/FAILED
  -> owns DeviceIdentity(BDF, generation)
  -> owns resource records
       +-- image VA intervals and section pages
       +-- tagged allocations and page runs
       +-- BAR/MMIO mapping handles
       +-- DMA buffer/mapping handles
       +-- IRQ hooks, work, and exported-call pins

device DMA request
  CPU virtual address
    -> physical pages
      -> DMA domain mapping or trusted direct/bounce backend
        -> device-visible DMA address
```

A raw kernel pointer is never a durable ownership identity. Public
driver-runtime objects use bounded slot-plus-generation handles. Internal
records retain the exact driver generation and, where applicable, bound PCI
device identity.

## Fixed Policies

### Driver And Device Identity

- Driver record reuse increments a nonzero generation.
- A resource owned by one driver generation cannot be freed, mapped, or used
  by a stale or different generation.
- PCI device identity includes segment when available and at minimum
  bus/device/function plus a discovery generation.
- PCI configuration, BAR, MMIO, DMA, IRQ, and bus-master operations require a
  live binding to the calling driver unless explicitly diagnostic.
- Resource tables are bounded and report exhaustion; they never silently
  overwrite a live record.

### Lifecycle And In-Flight Execution

- `LOADING` may allocate private setup resources but cannot publish public
  exports or enable bus mastering before commit.
- `ACTIVE` accepts new calls, IRQs, work, MMIO, and DMA operations.
- `QUIESCING` rejects new entry, masks/unregisters IRQ delivery, stops new
  work, disables device bus mastering, and waits for bounded in-flight pins.
- executable pages remain mapped until every CPU and callback can no longer
  return into the driver.
- timeout leaves the driver quarantined and mapped with a named diagnostic;
  it never frees code or DMA memory underneath live execution.

### Reusable Virtual Address Allocation

- Driver-image and PCI-MMIO arenas use reusable interval allocators, not
  monotonic cursors.
- allocations are page-aligned, overflow-safe, bounded by their arena, and
  reject overlap.
- free validates exact base, length, kind, owner, and generation.
- adjacent free intervals coalesce deterministically.
- stale/double/wrong-owner free is rejected without changing allocator state.
- image sections retain final W^X policy. MMIO mappings are always NX.
- a TLB-safe unmap completes before an interval or backing page is reused.
- optional guard pages surround unloadable driver images when capacity
  permits; guard policy must be explicit and measured.

### Driver Allocation API

The driver ABI gains owned allocation operations conceptually equivalent to:

```text
drv_alloc(size, alignment, flags, tag) -> allocation handle + CPU pointer
drv_free(allocation handle)
drv_alloc_pages(page_count, alignment, flags) -> page handle + CPU pointer
```

- size, alignment, flags, tag, owner, page count, and allocation context are
  recorded.
- zeroing is explicit; memory returned to another owner is cleared where
  confidentiality requires it.
- general heap, page-backed, physically contiguous, and DMA allocations are
  distinct classes.
- an allocation cannot be freed by pointer alone through the new ABI.
- unload reports and then reclaims ordinary owned allocations only after
  quiescence. DMA resources follow stricter device-stop ordering.

Legacy `kmalloc/kfree` imports are retained temporarily for linked core
drivers and migration, but new packaged hardware drivers use the owned API.
Closure records every remaining legacy user; unrestricted legacy imports are
removed from the external `.drv` ABI once migration completes.

### Allocation And Execution Context

Every driver entry has a checked context:

- `THREAD_SLEEPABLE`: may use ordinary owned allocation and bounded waits;
- `THREAD_ATOMIC`: may use only explicitly atomic/preallocated resources;
- `IRQ`: may not sleep and may use only IRQ-safe operations;
- `NMI/DOUBLE_FAULT`: may not call the ordinary driver runtime.

Atomic allocation uses a bounded reserve established before device
activation. Exhaustion fails immediately. No API hides sleeping, reclaim, or
cross-CPU acknowledgement behind an IRQ-safe name.

### Capability-Scoped MMIO

Raw MMIO virtual addresses are replaced at the external driver boundary by a
generation-checked mapping handle:

```text
drv_pci_map_bar(device, bar, offset, length, cache_policy) -> MMIO handle
drv_mmio_read8/16/32/64(handle, offset)
drv_mmio_write8/16/32/64(handle, offset, value)
drv_mmio_barrier(handle, direction)
drv_pci_unmap_bar(handle)
```

- mapping requires a live device binding and `PCI|MMIO` permission.
- BAR type, size, 64-bit pairing, requested subrange, integer overflow,
  register width, alignment, and live generation are checked.
- the first cache policies are `DEVICE_UC` and explicit framebuffer
  `WRITE_COMBINING` once PAT support is validated. Device memory is never
  silently mapped write-back.
- duplicate mappings may share one internal mapping only with reference and
  cache-policy compatibility.
- unload revokes handles and returns the VA interval after TLB-safe unmap.
- raw address access remains internal to the kernel backend and is not a
  packaged-driver capability.

### DMA Address Model

The ABI treats these as different values:

- CPU virtual address: dereferenced by the CPU;
- physical page address: PMM/paging identity, not automatically device-safe;
- DMA address: programmed into a device descriptor for one domain and mask.

No implicit cast between these address classes is valid.

A device declares its DMA mask before bus mastering. The runtime rejects
unrepresentable addresses or uses a bounded bounce buffer when policy permits.
Bus mastering is enabled only after the device has a live DMA domain and the
driver has committed all required queues.

### Coherent DMA

The first coherent API provides:

```text
drv_dma_set_mask(device, bits)
drv_dma_alloc_coherent(device, size, alignment)
drv_dma_free_coherent(handle)
```

- returns an owned handle, CPU mapping, device-visible DMA address, logical
  size, and backing-page count;
- uses zero-filled, NX pages;
- enforces alignment, boundary, mask, and bounded global/per-driver budgets;
- never exposes unrelated physical memory;
- frees only after device ownership stops and any required IOMMU invalidation
  completes;
- falls back to bounce/low memory only when explicitly supported and
  diagnosed.

### Streaming DMA And Scatter/Gather

Streaming mappings pin owned pages for a direction and lifetime:

```text
drv_dma_map_buffer(device, allocation, offset, length, direction)
drv_dma_map_sg(device, segments, direction)
drv_dma_sync_for_cpu(mapping)
drv_dma_sync_for_device(mapping)
drv_dma_unmap(mapping)
```

- direction is `TO_DEVICE`, `FROM_DEVICE`, or `BIDIRECTIONAL`;
- page/range, ownership, overlap, mask, segment count, boundary, and maximum
  transfer sizes are checked;
- mappings retain the source allocation until unmap;
- scatter/gather coalescing is bounded and records the final device segments;
- cache synchronization is explicit even when the initial x86 backend is
  coherent, preserving multi-architecture semantics;
- double unmap, wrong-device use, CPU access before required sync, and device
  access after unmap are diagnostic failures.

### IOMMU-Ready Domain Policy

- every DMA-capable device attaches to a `DmaDomain` abstraction before bus
  mastering;
- an IOMMU backend may allocate I/O virtual addresses, install mappings,
  invalidate translations, and report faults without changing driver ABI;
- a direct backend may return constrained physical addresses only for trusted
  native drivers when product policy explicitly permits it;
- `REQUIRE_ISOLATION` fails closed when no remapping backend exists;
- guest passthrough, untrusted driver DMA, and any claim of DMA isolation are
  forbidden on the direct backend;
- domain teardown disables bus mastering, drains mappings, performs required
  invalidation, and only then releases pages.

Phase 4.7 freezes this abstraction and safe fallback behavior. Full VT-d and
AMD-Vi feature coverage remains later work unless separately promoted into
the phase with its own hardware evidence.

### Diagnostics And Security

Diagnostics expose counts, generations, sizes, states, failures, high-water
marks, and owner names. Ordinary shell/user output does not expose kernel
virtual, physical, or DMA addresses. Diagnostic builds may emit addresses
only behind an explicit privileged command.

Permissions remain necessary but are not treated as range capabilities.
MMIO and DMA handles carry the actual device/range authority.

## 4.7A: Ownership And Lifetime Contracts

Goal: make resource identity and lifecycle explicit before adding new memory
APIs.

- add generation-tagged driver and device identities;
- define bounded resource kinds, ownership tables, counters, and lock order;
- define lifecycle transitions and entry-pin rules;
- add diagnostics for live/quarantined resources and in-flight execution;
- preserve current linked/package activation and dependency behavior.

Exit gate: stale/cross-driver identities and illegal lifecycle transitions are
rejected, while current driver regression remains green.

## 4.7B: Reusable Driver Virtual Address Space

Goal: remove monotonic VA exhaustion from driver image loading.

- replace the image cursor with a reusable interval allocator;
- preserve W^X, relocation, import patch, rollback, and unload ordering;
- add exact free, coalescing, fragmentation, guard, exhaustion, stale-free,
  and TLB-safe reuse tests;
- repeatedly load/unload different-sized packages until physical and VA
  baselines must match.

Exit gate: repeated package churn reuses a bounded VA set with zero overlap,
page drift, or executable stale mapping.

## 4.7C: Owned Allocation And Execution Contexts

Goal: associate driver memory with a live owner and legal calling context.

- implement tagged heap/page allocation handles and per-owner budgets;
- enforce sleepable, atomic, IRQ, and emergency context rules;
- provide a bounded atomic reserve;
- migrate packaged sample drivers away from raw `kmalloc/kfree`;
- report and reclaim leaked ordinary resources after quiescence.

Exit gate: wrong-owner, stale, double-free, budget, alignment, context, and
fault-rollback cases preserve exact resource state.

## 4.7D: Capability-Scoped MMIO

Goal: replace external raw-address MMIO with BAR-scoped mappings.

- add reusable PCI MMIO VA allocation;
- add generation-checked MMIO handles and checked-width accessors;
- validate binding, BAR range, subrange, alignment, cache policy, and owner;
- implement unmap, reference handling, automatic revocation, and diagnostics;
- migrate packaged PCI/MMIO samples from raw addresses.

Exit gate: a driver can access only its live mapped BAR range; repeated
map/unmap returns mappings and VA intervals to baseline.

## 4.7E: Coherent DMA And Address Model

Goal: establish the minimum safe bus-mastering memory path.

- freeze distinct CPU/physical/DMA address types;
- add device DMA masks, coherent allocation, alignment/boundary rules,
  zeroing, budgets, and bounce fallback;
- require a live DMA domain before enabling bus mastering;
- add a diagnostic QEMU DMA-capable device workload.

Exit gate: a real QEMU test device completes bounded DMA round trips through
the new API, while invalid masks, stale handles, device mismatch, and partial
allocation failures roll back exactly.

## 4.7F: Streaming DMA, Scatter/Gather, And Domain Policy

Goal: support non-coherent lifetime and fragmented buffers without exposing
raw pages.

- implement direction-checked streaming mappings and explicit sync;
- implement bounded scatter/gather building/coalescing and bounce handling;
- add the backend-neutral DMA-domain interface;
- implement the trusted direct backend and fail-closed isolation policy;
- test map/unmap versus concurrent IRQ/completion and device teardown.

Exit gate: every mapping has one live device/domain owner, pinned pages cannot
be freed early, and no direct backend is reported as isolated.

## 4.7G: Quiescent Unload And Automatic Cleanup

Goal: prove that unload cannot free resources or code still in use.

- transition `ACTIVE -> QUIESCING` and reject new entry;
- mask/unregister IRQs and cancel/drain driver work;
- disable bus mastering and drain/unmap DMA;
- revoke MMIO and exports, release bindings and owned allocations;
- wait for in-flight pins without holding locks required by exit paths;
- quarantine on timeout; free image pages only after all return paths stop.

Exit gate: unload races with IRQ, exported calls, work, MMIO, DMA completion,
and dependent activity without UAF, double cleanup, stale execution, or
resource drift.

## 4.7H: Fault Injection, Device Smoke, Soak, And Closure

Goal: certify the complete driver-memory runtime and inherited OS.

- inject VA-node, image-page, allocation-record, page-run, MMIO-record,
  page-map, DMA-record, bounce-buffer, domain-map, IRQ-drain, and quiesce
  failures;
- run one/four-vCPU driver load/use/unload and DMA completion races;
- run a minimum 60-second driver churn soak with package reload, MMIO,
  coherent/streaming/SG DMA, IRQ, service, and GUI activity;
- require identical warmed/final resource snapshots and no stalled CPU/device;
- add the aggregate only after every focused target exists;
- run clean normal/diagnostic builds, Phase 3.6 driver regression,
  `test-phase46`, the Phase 4.7 aggregate, and full project closure.

Exit gate: every Phase 4.7 matrix row and inherited gate passes from a clean
tree with immutable evidence.

## Required Order And Dependencies

```text
Phase 4.6 complete
  -> 4.7A -> 4.7B -> 4.7C -> 4.7D -> 4.7E -> 4.7F -> 4.7G -> 4.7H
       |
       +---- Phase 5 may proceed independently

4.7E/F/G complete
  -> production bus-mastering hardware drivers
```

No subphase may enable a device to DMA into arbitrary physical memory as a
shortcut. No unload optimization may weaken the quiescence boundary.
