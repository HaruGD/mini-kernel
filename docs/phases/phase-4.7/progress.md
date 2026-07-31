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
| 4.7C: Owned allocation and execution contexts | Planned | - | - | - | P47-R04, P47-R05 |
| 4.7D: Capability-scoped MMIO | Planned | - | - | - | P47-R06, P47-R07 |
| 4.7E: Coherent DMA and address model | Planned | - | - | - | P47-R08 |
| 4.7F: Streaming DMA, scatter/gather, and domain policy | Planned | - | - | - | P47-R09, P47-R10 |
| 4.7G: Quiescent unload and automatic cleanup | Planned | - | - | - | P47-R11 |
| 4.7H: Fault injection, device smoke, soak, and closure | Planned | - | - | - | P47-R12, P47-R13 |

Current status: Phase 4.7A and 4.7B are complete. Driver images now use an
owned, reusable interval arena with guard pages, deterministic coalescing,
W^X preservation, and acknowledged kernel-global TLB invalidation before VA
reuse. Phase 4.7C is next.

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
