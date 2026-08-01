# Phase 4.7 Progress

This is the live implementation ledger for driver memory and DMA. Stable
policy belongs in [implementation_plan.md](implementation_plan.md); required
coverage belongs in [regression_matrix.md](regression_matrix.md).

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but its exit gate has not passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: the subphase exit gate passed and immutable evidence exists.

Reserved target names are plans, not executed evidence. Completion requires an
implementation commit, exact commands, resource measurements, negative
coverage, and a separate evidence commit.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4.7A: Ownership and lifetime contracts | Complete | 2026-08-01 | 2026-08-01 | `ddb0772` | P47-R01 |
| 4.7B: Reusable driver virtual address space | Complete | 2026-08-01 | 2026-08-01 | `e93813a`, `89be05e` | P47-R02, P47-R03 |
| 4.7C: Owned allocation and execution contexts | Complete | 2026-08-01 | 2026-08-01 | `39c440e`, `f1875a3`, `fe6afbf` | P47-R04, P47-R05 |
| 4.7D: Capability-scoped MMIO | Complete | 2026-08-01 | 2026-08-01 | `e00cb72`, `257526f` | P47-R06, P47-R07 |
| 4.7E: Coherent DMA and address model | Complete | 2026-08-01 | 2026-08-01 | `257526f`, `39e53f3` | P47-R08 |
| 4.7F: Streaming DMA, scatter/gather, and domain policy | Complete | 2026-08-01 | 2026-08-01 | `94202fe`, `39e53f3`, `4019a2d` | P47-R09, P47-R10 |
| 4.7G: Quiescent unload and automatic cleanup | Complete | 2026-08-01 | 2026-08-01 | `1314b15` | P47-R11 |
| 4.7H: Fault injection, device smoke, soak, and closure | Complete | 2026-08-01 | 2026-08-01 | `1a4907f` | P47-R12, P47-R13 |

Current status: Phase 4.7A through 4.7H are complete. Packaged drivers now use
generation-owned allocation, MMIO, coherent DMA, streaming DMA, SG, and
domain handles. The trusted direct backend is functional but not isolated;
quiescent unload now closes entry before draining calls, IRQ, work, and DMA
pins. Deterministic fault rollback, real QEMU EDU device transfers, multicore
execution, and the required 60-second drift-free soak close Phase 4.7.

## Implementation Order

```text
4.7A ownership/lifetime contracts
  -> 4.7B reusable driver VA
  -> 4.7C owned allocation/context rules
  -> 4.7D capability MMIO
  -> 4.7E coherent DMA/address model
  -> 4.7F streaming DMA/SG/domain policy
  -> 4.7G quiescent unload
  -> 4.7H fault/soak/closure
```

## Recording Workflow

For each subphase:

1. mark only that subphase `In progress`;
2. implement bounded behavior and focused positive/negative tests;
3. run the subphase gate and every affected inherited suite;
4. commit implementation and tests;
5. record the immutable implementation hash and measured evidence;
6. mark the row complete only when every required result passes;
7. commit evidence separately.

## 4.7A: Ownership And Lifetime Contracts

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commit: `ddb0772`

### Delivered

- Added generation-tagged driver and PCI-binding identities with stale and
  cross-owner rejection.
- Added a bounded 128-slot resource registry for exports, PCI bindings, IRQ
  hooks, and later image/allocation/MMIO/DMA resource classes.
- Bound module exports, PCI bindings, and IRQ hooks to the exact live driver
  generation instead of a reusable name alone.
- Enforced legal `REGISTERED`, `LOADING`, `LINKED`, `READY`, `QUIESCING`,
  failure, and rejection transitions. Quiescing drivers cannot publish new
  resources, resolve exports, or receive driver IRQ dispatch.
- Added active/high-water/per-kind and denial diagnostics while preserving the
  established `drivers` output prefix used by boot regression parsers.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-driver-ownership` | PASS | Stale lifecycle and cross-owner cases passed; four host threads completed 4,000 concurrent register/release cycles; 128 live records reached exact exhaustion and returned to zero |
| `make -j4 uefi` | PASS | Kernel, driver runtime, packaged drivers, root image, and UEFI image built successfully |
| `make test-driver-regression` | PASS | Policy/layout/build seven-combination coverage, signed/unsigned/tampered/bounded/dependency boot handoff, UEFI smoke, SDK 91/91, and R01-R12 passed |

### Resource Accounting

- Resource-table capacity/high-water: `128/128` in the deterministic
  exhaustion case.
- Active resources after owner cleanup and after concurrent churn: `0`.
- Stale driver identity after same-slot reuse remained invalid; the new
  generation remained live.
- Unexplained resource drift: zero.

### Remaining

- 4.7C adds tagged owned heap/page allocation and checked execution-context
  rules.

## 4.7B: Reusable Driver Virtual Address Space

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commits: `e93813a`, `89be05e`

### Delivered

- Replaced the monotonic image cursor with a bounded, first-fit reusable
  interval arena covering 32,768 pages.
- Added exact generation-owned VA handles, sorted insertion, overlap checks,
  adjacent coalescing, exhaustion accounting, and stale/double/wrong-owner
  rejection.
- Reserved one unmapped guard page on each side of every loaded section and
  registered each usable image mapping in the driver resource registry.
- Preserved initial `RW/NX` relocation/import patching followed by final
  `CODE=RX`, `RODATA=R/NX`, and `DATA/BSS=RW/NX` protection.
- Added a kernel-global SMP TLB shootdown mode. Image VA is released only
  after unmap and acknowledgement from every online CPU; failure quarantines
  the interval instead of making it reusable.
- Deferred PMM page return until that acknowledgement completes, so a stale
  translation cannot observe a page already reassigned to another owner.
- Added address-free shell diagnostics for active image intervals, free and
  largest-free pages, and quarantine count.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-driver-va` | PASS | Mixed 2/3/1-page allocation, freed-low-address reuse, exact 16-page coalescing, exact-limit exhaustion, stale/double/wrong-owner rejection, and quarantine retention passed |
| `make test-driver-image-memory` | PASS | Monotonic cursor absence, two-sided guard policy, W^X flags, and unmap -> SMP acknowledgement -> resource release -> VA release ordering passed |
| `make test-tlb-shootdown test-tlb-lock-order` | PASS | 64-cycle SMP smoke completed with AP acknowledgements and the existing no-lock-wait/quarantine model passed |
| `make test-driver-ownership` | PASS | Generation-owned resource regression and 4,000 concurrent resource cycles remained green |
| `make test-driver-boot` | PASS | Signed, unsigned, tampered, bounded, and dependency handoff cases passed |
| `make test-uefi-smoke` | PASS | Real `.drv` load, unload, second load, reload, dependencies, one/five-section packages, and user/kernel regression commands passed; quarantine remained zero |
| `make -j4 uefi` | PASS | Kernel, root image, packaged drivers, and UEFI image rebuilt successfully |

### Resource Accounting

- Arena capacity/free baseline after a complete host release: `16/16` pages
  with one coalesced free interval in the bounded test arena.
- Production image arena capacity: `32,768` pages; every active section adds
  two unmapped guard pages.
- Exact-limit allocation: 14 usable plus two guard pages consumed the
  16-page test arena; the next allocation failed without state drift.
- UEFI smoke diagnostic quarantine count: `0` before and after manual driver
  lifecycle commands.
- A deliberately quarantined four-page reserved interval was retained and
  rejected on release in the negative host case.

### Remaining

- Physical-page baseline and extended mixed-package churn remain part of the
  final Phase 4.7 soak, while 4.7C now adds owned heap/page allocations and
  legal execution contexts.

## 4.7C: Owned Allocation And Execution Contexts

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commits: `39c440e`, `f1875a3`, `fe6afbf`

### Delivered

- Added `drv_alloc`/`drv_free` to the packaged-driver SDK. The ABI returns a
  slot-plus-generation handle, CPU pointer, and logical size; freeing by raw
  pointer is not supported by the new interface.
- Added a bounded 128-record allocation table with exact driver-generation
  ownership, tags, flags, alignment, charged bytes, backing class, resource
  handle, state, and diagnostics.
- Enforced a 1 MiB per-driver budget and distinct ordinary heap, page-backed
  NX, and bounded atomic-reserve allocation paths.
- Added eight 256-byte, 16-byte-aligned atomic reserve slots per driver. IRQ
  and thread-atomic contexts may use only this reserve and fail immediately
  when it is exhausted.
- Added per-CPU nested execution contexts. Driver entry/exit execute as
  `THREAD_SLEEPABLE`; registered IRQ callbacks execute as `IRQ`; emergency
  context rejects every ordinary driver allocation operation.
- PCI probe callbacks also enter the owning driver's sleepable context before
  using PCI imports; a UEFI negative run caught and verified this boundary.
- Restricted VFS driver imports and legacy `kmalloc` to sleepable driver
  execution. Legacy `kmalloc/kfree` symbols remain temporarily available for
  migration, while new code uses owned handles.
- Added deterministic allocation backing fault injection, page-allocation
  rollback, zero-on-request and confidentiality clearing, automatic ordinary
  allocation reclamation after quiescing, and quarantine-on-TLB-failure.
- Migrated `hello_c.drv` to exercise owned scratch allocation and exact handle
  release during real packaged-driver activation.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-driver-alloc` | PASS | 128/128 exact slot exhaustion, 1 MiB budget boundary, alignment/zeroing, wrong-owner/stale/double release, injected page-backing failure and retry, two leaked-object auto-reclaim, and zero final resources passed |
| `make test-driver-context` | PASS | Eight IRQ atomic slots succeeded, the ninth failed immediately, ordinary/page IRQ allocation and emergency allocation were rejected, nested current-owner entry worked, and four threads completed 1,000 allocation/release cycles |
| `make test-driver-ownership test-driver-va test-driver-image-memory` | PASS | Existing generation, resource, image VA, W^X, guard, and TLB-retirement contracts remained green |
| `make test-fault-injection` | PASS | Existing host and QEMU fault suites passed with the new `driver_alloc` injection point |
| `make -j4 uefi` | PASS | New runtime object, SDK imports, sample package, kernel, root image, and UEFI image built successfully |
| `make test-uefi-smoke` | PASS | `hello_c.drv` used the new owned API during automatic activation; manual driver lifecycle and system commands passed with allocation active/bytes/quarantine all zero |
| `make test-driver-regression` | PASS | Seven policy/build combinations, signed/unsigned/tampered/bounded/dependency handoff, UEFI smoke, SDK 91/91, and R01-R12 passed |

### Resource Accounting

- Allocation-record high-water/capacity: `128/128`; final active records and
  charged bytes: `0/0`.
- Per-driver budget boundary: an exact `1,048,576`-byte allocation succeeded;
  one additional byte failed without drift.
- Atomic reserve boundary: `8/8` slots succeeded; slot nine failed; final
  atomic-active count was zero.
- Deterministic backing failure count: one; the same page range was reusable
  immediately after rollback.
- Concurrent churn: four host threads completed 250 cycles each; unexplained
  allocation or resource drift was zero.
- UEFI smoke before/final diagnostics: `alloc_active=0`, `alloc_bytes=0`,
  `alloc_quarantine=0`.

### Remaining

- 4.7D builds BAR-scoped MMIO handles and a reusable NX MMIO VA arena on top
  of these owner, context, resource, and deferred-retirement contracts.

## 4.7D: Capability-Scoped MMIO

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commits: `e00cb72`, `257526f`

### Delivered

- Replaced packaged-driver raw BAR pointers and raw-address reads/writes with
  binding-, owner-, and generation-checked handles.
- Added 8/16/32/64-bit alignment and range validation, UC-only initial cache
  policy, compatible mapping references, barriers, automatic owner revoke,
  and a reusable 64 MiB NX MMIO VA arena.
- Split handle revocation from TLB acknowledgement so no MMIO allocator lock
  is held while waiting for other CPUs.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-driver-mmio` | PASS | Wrong owner/device/generation, I/O BAR, overflow, alignment, range, cache, stale and double-unmap cases were rejected |
| `make test-pci-mmio-va` | PASS | One shared handle reference and 256 mixed subrange map/unmap cycles returned all 16,384 arena pages |
| `make test-uefi-smoke` | PASS | Packaged PCI sample mapped and revoked a real QEMU VGA BAR capability; `mmio_active=0`, `mmio_free_pages=0x4000`, quarantine zero |

## 4.7E: Coherent DMA And Address Model

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commits: `257526f`, `39e53f3`

### Delivered

- Froze distinct CPU pointer and device-visible DMA address types and added
  generation-owned domain and coherent-buffer handles.
- Added 24-64-bit masks, physical alignment/boundary constraints, zero-filled
  NX pages, 4 MiB per-owner and 16 MiB global budgets, constrained low-memory
  allocation, and bus-master enable/disable ordering.
- Added trusted-direct domains that explicitly report no isolation;
  `REQUIRE_ISOLATION` fails closed without a remapping backend.
- Added QEMU EDU hardware evidence: 256 bytes completed
  `RAM -> EDU internal buffer -> RAM` and matched byte-for-byte.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-dma-coherent` | PASS | Mask, zeroing, 8 KiB alignment, 64 KiB boundary, budget, wrong-owner, stale, bus-master ordering and automatic cleanup passed |
| `make test-uefi-smoke` | PASS | EDU BAR and DMA handles completed the physical round trip; final coherent buffers/bytes/quarantine were `0/0/0` |

### Resource Accounting

- QEMU smoke retains two live trusted-direct domains for the two bound sample
  devices while the driver is active; transient coherent buffers, bytes,
  MMIO mappings, and quarantine all return to zero.
- No output exposes CPU virtual, physical, or DMA addresses through ordinary
  driver diagnostics.

## 4.7F: Streaming DMA, Scatter/Gather, And Domain Policy

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commits: `94202fe`, `39e53f3`, `4019a2d`

### Delivered

- Added exact owned-allocation pin/unpin lifetime, TO/FROM/BIDIRECTIONAL
  direction state, explicit CPU/device synchronization, and stale/double
  unmap rejection.
- Added bounded 16-source/32-segment SG generation with adjacent physical
  coalescing and a bounded coherent bounce mapping when a direct segment
  cannot satisfy the device mask.
- Source free is denied while pinned. Automatic owner cleanup synchronizes
  receive mappings, unmaps streaming/SG mappings, releases bounce/coherent
  buffers, disables bus mastering, and then removes direct domains.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-dma-streaming` | PASS | Four buffer/SG mappings covered all directions, sync violations, pinned free, stale unmap, 32-segment overflow, 24-bit bounce fallback, and zero final pins/maps |
| `make test-dma-domain` | PASS | Isolation-required policy failed closed and trusted direct mode never claimed isolation |
| `make test-driver-alloc test-driver-mmio` | PASS | Allocation pin additions and prior MMIO ownership/reuse contracts remained green |
| `make test-uefi-smoke` | PASS | Packaged driver completed real page-backed streaming map/sync/unmap/free plus EDU coherent DMA; final streaming/pinned counts were `0/0` |

### Remaining

- Hardware-remapped IOMMU isolation remains later hardware-track work; the
  Phase 4.7 trusted-direct fallback continues to report no isolation.

## 4.7G: Quiescent Unload And Automatic Cleanup

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commit: `1314b15`

### Delivered

- Added generation-owned call, IRQ, work, and DMA activity pins with a
  close-admission/recheck protocol that resolves the entry-versus-quiesce
  race without holding a lock across the drain wait.
- Made driver execution entry automatically pin ordinary calls and IRQs;
  IRQ dispatch snapshots the callback before admission so unregister cannot
  invalidate an already admitted return path.
- Reordered unload to enter `QUIESCING`, reject new entry, unregister IRQ
  admission, disable every owned PCI bus master, drain all execution pins,
  invoke the privileged exit path, and release DMA, MMIO, bindings,
  allocations, and executable image pages in dependency order.
- Added a bounded wait. Timeout retains the driver and its resources in a
  failed/quarantined state instead of freeing code that can still execute.
- Added address-free in-flight, quiescing, and timeout diagnostics.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-driver-quiesce` | PASS | Four activity kinds drained exactly; new entry and stale generation were rejected; an intentional timeout quarantined state; four host threads crossed the admission boundary without a residual pin; teardown order was exact |
| `make test-driver-ownership test-driver-alloc test-driver-context` | PASS | Existing lifecycle, 4,000 resource cycles, allocation ownership, IRQ reserve, and execution-context contracts remained green |
| `make test-driver-mmio test-dma-coherent test-dma-streaming test-dma-domain` | PASS | MMIO, coherent/streaming/SG DMA, bounce, pin, sync, mask, and direct-domain contracts remained green |
| `make -j4 uefi` | PASS | Kernel, packaged drivers, root image, and UEFI image rebuilt with the quiesce boundary |
| `make test-uefi-smoke` | PASS | Automatic/manual package lifecycle, QEMU EDU DMA, services, GUI, and kernel/user regression commands completed without panic or quarantine |

### Resource Accounting

- Focused timeout case: four admitted pins, four exact unpins, one rejected
  post-quiesce entry, one timeout, and one retained quarantine record.
- Successful retry after drain observed zero call, IRQ, work, DMA, and total
  in-flight pins.
- Four host workers completed at least 1,000 admitted work-pin cycles before
  the gate closed; no entry succeeded after quiescence.
- UEFI lifecycle returned transient MMIO, coherent DMA, streaming DMA,
  allocation, and pin counts to zero.

### Remaining

- Phase 4.7H closure certification is recorded below.

## 4.7H: Fault Injection, Device Smoke, Soak, And Closure

- Status: Complete
- Started: 2026-08-01
- Completed: 2026-08-01
- Implementation commit: `1a4907f`

### Delivered

- Added generic PCI INTx IDT/APIC routing and generation-owned IRQ admission,
  dispatch, acknowledgement, masking, and unregister behavior.
- Extended deterministic driver-memory fault injection across VA records,
  image pages, allocation records, page runs, MMIO records/maps, DMA records,
  bounce buffers, DMA domains, IRQ drain, and quiesce drain paths.
- Upgraded the packaged QEMU EDU driver to execute checked coherent,
  streaming FROM_DEVICE, and two-source SG TO_DEVICE transfers. Clean one- and
  four-vCPU sessions also require a real interrupt completion and device
  acknowledgement.
- Added bounded rollback/retry checks for every new failure point, a QEMU
  diagnostic fault run, one/four-vCPU device smoke, and a four-vCPU 60-second
  driver/GUI churn soak.
- Added `test-phase47` as the nonempty aggregate of every Phase 4.7 focused,
  fault, device, and soak target.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-driver-memory-faults` | PASS | Every host-reachable allocation/mapping failure rolled back and the same operation succeeded on retry |
| `make test-driver-dma-device` | PASS | Clean one- and four-vCPU QEMU EDU sessions completed IRQ acknowledgement plus exact 512-byte coherent, streaming, and two-source SG data checks |
| `make test-driver-memory-soak` | PASS | Four-vCPU QEMU ran for 60 seconds with 18 driver reload cycles and 28 GUI cycles; warmed and final resource snapshots were identical |
| `make test-phase47` | PASS | P47-R01 through P47-R13 focused, diagnostic-QEMU, device, and soak gates passed in one aggregate run |
| `make test-driver-regression` | PASS | Driver policy/build/handoff, UEFI smoke, SDK 91/91, and the inherited driver R01-R12 matrix passed |
| `make test-phase46` | PASS | One/two/four-vCPU topology, concurrent execution, remote wake, TLB shootdown, IRQ ownership, fault injection, and one/four-vCPU 60-second soaks passed |
| `make test-closure` | PASS | ABI freeze, driver, graphics, window, GUI recovery, fault, soak, boot, input, IPC, and service closure targets passed |

### Resource Accounting

- The 60-second soak warmed/final system snapshot was exactly
  `(cpus=4, processes=21, windows=1, surfaces=4, pmm_free=26898,
  heap_used=1946016, heap_mapped=1982464)`.
- The warmed/final driver snapshot was exactly
  `(resources=43, bindings=2, drivers=1, image_sections=37,
  image_pages=33, image_va_free=32668, mmio_active=0,
  mmio_free_pages=16384, dma_domains=2)` with zero active allocations,
  coherent buffers, streaming maps, pinned ranges, quiescing drivers,
  in-flight pins, timeouts, and quarantines.
- The soak observed no unexplained process, thread, page, heap, surface,
  binding, MMIO, DMA, IRQ, or driver-generation drift.

### Remaining

- No Phase 4.7 work remains. Full VT-d/AMD-Vi remapping, interrupt remapping,
  and production storage/USB/network/audio/GPU drivers remain later roadmap
  work.

## Evidence Record Template

```text
## 4.7X: Name

- Status: Complete
- Started: YYYY-MM-DD
- Completed: YYYY-MM-DD
- Implementation commit(s): `<hashes>`

### Delivered

- concrete ownership, mapping, DMA, or unload result;
- failure and fallback policy;
- diagnostics and tests.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <real-target>` | PASS | handles, pages, VA ranges, DMA maps, cycles |

### Resource Accounting

- driver generation and device identity;
- owned allocations, pages, VA intervals, BAR maps, DMA maps, IRQs, and
  in-flight calls before/after;
- physical, virtual, and device address evidence without leaking addresses to
  ordinary user diagnostics;
- injected failure counts and unexplained drift;
- remaining deferred work.
```
