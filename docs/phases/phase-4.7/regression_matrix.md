# Phase 4.7 Regression Matrix

This matrix defines the evidence required to close driver memory and DMA.
Every target prefixed `Reserved:` is a planned name only. It must be replaced
with a real command after implementation and a passing run.

The future aggregate name is `make test-phase47`. It must invoke real focused
targets and must not be an empty or documentation-only target.

## Matrix

| ID | Subphase | Contract | Planned automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P47-R01 | 4.7A | Every driver/device/resource reference has a live generation owner and legal lifecycle state. | `make test-driver-ownership` | Stale, cross-driver, wrong-device, overflow, duplicate publication, and illegal lifecycle transitions are rejected without state drift. | Complete |
| P47-R02 | 4.7B | Driver image VA intervals are reusable, nonoverlapping, and exactly owned. | `make test-driver-va` | Split/coalesce, exact-fit, fragmentation, exhaustion, stale/double/wrong-owner free, and repeated mixed-size reuse preserve allocator invariants. | Complete |
| P47-R03 | 4.7B | Reusable image mapping preserves package validation, W^X, rollback, and TLB-safe unload. | `make test-driver-image-memory`; `make test-uefi-smoke` | CODE remains RX, RODATA R/NX, DATA/BSS RW/NX; relocation/load/protect failures release pages and intervals; no CPU executes or resolves stale unloaded code. | Complete |
| P47-R04 | 4.7C | Driver heap/page allocations are tagged, bounded, generation-owned, and reclaimable. | `make test-driver-alloc` | Alignment, zeroing, budget, exhaustion, wrong-owner, stale, double-free, partial-page failure, leak report, and post-quiesce reclaim retain exact counts. | Complete |
| P47-R05 | 4.7C | Sleepable, atomic, IRQ, and emergency contexts permit only legal allocation/blocking operations. | `make test-driver-context` | IRQ allocation uses only the bounded reserve; sleep/block from atomic/IRQ and all ordinary runtime calls from NMI/DF are rejected and counted. | Complete |
| P47-R06 | 4.7D | MMIO authority is limited to one live bound BAR mapping and checked offset. | Reserved: `make test-driver-mmio` | Wrong binding/owner/device/generation, I/O BAR, zero/overflow range, misalignment, out-of-range width, incompatible cache policy, and use-after-unmap fail without access. | Planned |
| P47-R07 | 4.7D | PCI MMIO VA mappings unmap and reuse safely. | Reserved: `make test-pci-mmio-va` | Repeated subrange/full-BAR map/unmap, compatible sharing, reference release, coalescing, exhaustion, and fault rollback return VA/page/map counts to baseline. | Planned |
| P47-R08 | 4.7E | Coherent DMA returns distinct CPU and device addresses obeying owner, mask, alignment, boundary, zeroing, budget, and teardown rules. | Reserved: `make test-dma-coherent` | 32/64-bit masks, low/bounce fallback, impossible constraints, stale/wrong-device free, partial failures, and bus-master ordering pass with zero drift. | Planned |
| P47-R09 | 4.7F | Streaming and scatter/gather mappings pin exact ranges for one device, direction, and sync lifetime. | Reserved: `make test-dma-streaming` | Fragmented/coalesced/bounced mappings, segment limits, offset overflow, premature free, sync-order violations, duplicate unmap, concurrent completion, and cleanup are deterministic. | Planned |
| P47-R10 | 4.7F | DMA domains distinguish remapped isolation from trusted direct fallback. | Reserved: `make test-dma-domain` | `REQUIRE_ISOLATION` fails without an IOMMU; direct mode enforces mask/ownership and never claims isolation; map/unmap/invalidate failures quarantine pages. | Planned |
| P47-R11 | 4.7G | Unload quiesces all execution and device access before revoking resources or code. | Reserved: `make test-driver-quiesce` | New entry is rejected; IRQ/work/export pins drain; bus mastering stops; DMA/MMIO/allocations release in order; timeout quarantines instead of freeing live state. | Planned |
| P47-R12 | 4.7H | A QEMU DMA-capable PCI device performs checked MMIO, IRQ, coherent, streaming, and SG round trips. | Reserved: `make test-driver-dma-device` | One/four-vCPU sessions complete exact data checks and interrupt acknowledgements with no raw-address ABI use, fault, stale completion, or resource drift. | Planned |
| P47-R13 | 4.7H | Repeated driver-memory churn and every inherited gate pass together. | Reserved: `make test-driver-memory-soak`; future `make test-phase47`; existing `make test-driver-regression`, `make test-phase46`, and `make test-closure` | Minimum 60-second one/four-vCPU churn reports cycles and identical warmed/final driver/page/VA/MMIO/DMA/IRQ/process/thread resources; clean aggregate and inherited suites pass. | Planned |

## Mandatory Negative Coverage

- driver/device/resource slot exhaustion and generation wrap policy;
- stale, wrong-owner, wrong-device, wrong-kind, duplicate, and illegal-state
  handles;
- VA integer overflow, overlap, fragmentation, exact-limit exhaustion,
  stale/double free, page-map/protect/unmap failure, and reuse before TLB
  completion;
- allocation size/alignment overflow, budget exhaustion, partial contiguous
  page failure, atomic reserve exhaustion, and sleeping allocation from IRQ;
- BAR index/type/size errors, 32/64-bit BAR pair errors, offset+width overflow,
  misalignment, cache-policy conflict, unmap failure, and access after revoke;
- DMA mask mismatch, impossible alignment/boundary, empty/oversized transfer,
  SG overflow, bounce exhaustion, wrong direction, missing sync, duplicate
  unmap, and source release while pinned;
- domain map/invalidate failure, direct fallback when isolation is required,
  bus master enabled before domain readiness, and bus master still enabled
  during page release;
- unload versus IRQ, exported call, worker, DMA completion, dependent load,
  process/service restart, and another CPU returning from driver code;
- one-vCPU fallback and four-vCPU concurrency.

## Resource Evidence

Every resource-sensitive target records warmed and final values for:

- live driver records and generations;
- image VA allocated/free intervals, section mappings, pages, and guard pages;
- owned heap/page allocations, bytes, tags, budgets, and atomic reserve;
- PCI bindings, MMIO handles, references, pages, and VA intervals;
- DMA domains, coherent buffers, streaming mappings, SG segments, bounce
  pages, pinned pages, and quarantined pages;
- IRQ hooks, in-flight IRQ/call/work pins, quiescing drivers, and timeouts;
- PMM free pages and kernel heap used/mapped values.

An expected cache/high-water change must be named separately from active
resource counts. Unexplained drift blocks completion.

## Evidence Rules

- Host models may close isolated allocators and parsers, but QEMU execution is
  mandatory for page permissions, MMIO, IRQ, DMA, SMP completion races, and
  unload return-path safety.
- The QEMU device, machine, CPU count, DMA mask, transfer sizes, directions,
  segment counts, IRQ counts, and cycle counts are recorded.
- A direct DMA backend is not evidence of IOMMU isolation.
- A passing aggregate cannot hide a failed focused row.
- P47-R01 through P47-R12 all block P47-R13 and Phase 4.7 closure.
- The optional one-hour release soak is useful but does not replace the
  repeatable required 60-second soak.
