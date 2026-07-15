# Phase 4 Progress

This is the live implementation ledger for Phase 4. The frozen architecture
belongs in `compositor_contracts.md`, and planned work belongs in
`implementation_plan.md`. This file records only work that has actually
started or completed and the reproducible evidence for that state.

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but the subphase exit gate has not
  passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: every exit-gate requirement has passed and an evidence record is
  present below.

A checkbox or status label alone is not completion evidence. `Complete`
requires an implementation commit, exact verification commands and results,
measured resource results where applicable, and a separate evidence commit
that records those immutable implementation commit hashes.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4A: Page-backed surface foundation | Complete | 2026-07-14 | 2026-07-14 | `196339c` | [record](#4a-page-backed-surface-foundation) |
| 4B: Surface ABI, mapping, and transfer rights | Complete | 2026-07-15 | 2026-07-15 | `b14bca1` | [record](#4b-surface-abi-mapping-and-transfer-rights) |
| 4C: Display-service present path | Complete | 2026-07-15 | 2026-07-15 | `23ee0f0` | [record](#4c-display-service-present-path) |
| 4D: Single-window bring-up | Complete | 2026-07-15 | 2026-07-15 | `231f27e` | [record](#4d-single-window-bring-up) |
| 4E: Multiwindow compositor | Planned | - | - | - | - |
| 4F: Input routing and focus | Planned | - | - | - | - |
| 4G: Window SDK and first GUI application | Planned | - | - | - | - |
| 4H: Lifecycle, fault, regression, and closure | Planned | - | - | - | - |

Current work: Phase 4D is complete. Phase 4E remains Planned.

## Recording Workflow

For each subphase:

1. change its status to `In progress` when implementation begins;
2. implement code and focused tests without weakening an earlier boundary;
3. run the subphase exit-gate commands from a clean, known commit;
4. commit the implementation and tests;
5. add the dated evidence record below, referencing the implementation commit
   or commits and the exact test results;
6. update the status table and roadmap checkbox to `Complete` only after every
   exit-gate condition passes;
7. commit the documentation as a separate subphase evidence/closure commit.

The evidence commit is separate because a Git commit cannot include its own
final hash in its contents. Do not insert a predicted or abbreviated hash
before the implementation commit exists.

## Evidence Record Format

Append one record per completed subphase using this format:

```text
## 4X: Subphase Name

Status: Complete
Started: YYYY-MM-DD
Completed: YYYY-MM-DD
Implementation commits: `<full or unambiguous commit hashes>`

### Delivered

- concrete externally observable implementation result;
- ABI, ownership, cleanup, or service-policy result;
- tests and diagnostics added with the implementation.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <focused-target>` | PASS | relevant counts/timing |
| `make <qemu-target>` | PASS | visible or serial marker |

### Resource Accounting

- warmed baseline: handles/pages/regions/objects/processes;
- final sample: handles/pages/regions/objects/processes;
- unexplained drift: zero or a documented blocker.

### Remaining

- work explicitly assigned to the next subphase;
- accepted limitation already listed in the implementation plan.
```

Do not use a completion record to hide partial failures. A failed required test
leaves the subphase `In progress`, with the failure summarized in the working
notes or issue tracker until corrected.

## 4A: Page-Backed Surface Foundation

- Status: Complete
- Started: 2026-07-14
- Completed: 2026-07-14
- Implementation commit: `196339c0b9350ddd29218f6313bcc5297987c27c`

### Delivered

- replaced graphics-surface kernel-heap pixels with individually allocated,
  zero-filled PMM pages;
- added fixed per-object kernel virtual slots in `0x78000000-0x80000000`,
  producing a contiguous software-rendering view over non-contiguous physical
  pages without overlapping heap, PCI MMIO, or driver-section arenas;
- enforced 16 object slots, 2,025 pages per maximum 1920x1080 surface, and an
  8,192-page global surface budget before allocation;
- added complete rollback for partial PMM and page-map failure, plus quarantined
  retry behavior when a low-level unmap fails;
- returned every page and kernel mapping on the final surface reference and
  exposed logical bytes, backing bytes, and page counts in diagnostics;
- added a real-kernel `surfacetest`, focused host tests, a QEMU smoke, and a
  reusable MM host fixture for object/process/IPC/service tests;
- wired the focused host test and QEMU smoke into the graphics and full closure
  regression paths.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-surface-backing` | PASS | Host allocation/zero/limit/rollback/unmap-retry coverage and QEMU two-page cross-boundary read/write. |
| `make test-graphics` | PASS | All graphics contract tests, user graphics demo, and GOP present at 1280x800 and 800x600. |
| `make test-fault-injection` | PASS | Host fault suite and diagnostic QEMU smoke passed after direct-link test migration. |
| `make test-uefi-screen` | PASS | 1280x800 framebuffer, 30,372 visible pixels. |
| `make test-closure` | PASS in 465.9 seconds | SDK 73/73, driver policy/build/boot matrix, boot/userland, graphics, input, IPC, services, concurrency, fault injection, and 60-second soak with 42 cycles. |
| `make clean && make -j2 uefi && python3 tools/surface_backing_smoke.py` | PASS | Clean parallel image build followed by the same two-page QEMU check. |

### Resource Accounting

- QEMU test surface: 1025x1 RGB, 4,100 logical bytes, two PMM pages;
- warmed PMM baseline: `0x00006A59` free pages;
- final PMM sample: `0x00006A59` free pages;
- final surface backing state: zero active backings, zero mapped surface pages,
  zero unmap failures;
- host fault cases: partial PMM allocation, partial VM mapping, global budget
  exhaustion, and one injected unmap failure all returned to zero allocated and
  mapped pages after cleanup/retry;
- unexplained resource drift: zero.

### Remaining

- user-visible surface create/info/map/unmap/close syscalls and SDK wrappers are
  Phase 4B work;
- per-process user mapping records, NX/user protection, rights attenuation,
  transfer lifetime, and process-exit mapping cleanup remain Phase 4B work;
- no user process can map the new backing pages yet.

## 4B: Surface ABI, Mapping, And Transfer Rights

- Status: Complete
- Started: 2026-07-15
- Completed: 2026-07-15
- Implementation commit: `b14bca144380c7a1120230feb90fd1d17e510145`

### Delivered

- added stable surface ABI v1 syscalls and User SDK wrappers for create,
  metadata query, process-local map/unmap, and close;
- reserved the bounded `0x10000000-0x18000000` user surface arena and added 16
  generation-tagged per-process mapping records without overlapping execution
  slots or kernel virtual arenas;
- installed every user surface page as user-accessible and NX, adding writable
  PTE and region rights only when the local handle has `WRITE | MAP`;
- added transactional partial-map rollback, exact-address unmap validation,
  bounded unmap retry, stale/wrong-handle rejection, close-time cleanup, and
  normal exit, kill, and user-fault cleanup before handle references release;
- added subset-checked handle cloning and made IPC v2 graphics-surface transfer
  produce exactly `READ | MAP`, removing write and re-transfer authority while
  preserving the sender's local rights;
- added least-privilege GUI application/service permission profiles while
  retaining explicit compatibility permissions for existing diagnostics;
- added host mapping/transfer/failure tests, SDK ABI/integration checks, a
  restricted-service permission check, and a QEMU mapped-exit resource-drift
  smoke wired into full closure.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make clean && make -j2 uefi` | PASS | Clean parallel kernel, SDK, userland, driver, FAT32, and ESP image build. |
| `make test-surface-abi` | PASS | Writable and read-only mappings, NX/PTE rights, subset cloning, handle/region/page-table exhaustion rollback, transfer lifetime, and QEMU mapped-exit cleanup. |
| `make test-user-sdk` | PASS | 91 passed, 0 failed, including 18 surface ABI and lifecycle checks. |
| `make test-service-manager-smoke` | PASS | Restricted service denied raw display, discovery, child launch, and shared-surface creation. |
| `make test-concurrency` | PASS | Spinlock, handle, process lifecycle, service registry, IPC mailbox, and attenuated IPC transfer host suites. |
| `make test-closure` | PASS in 483.542 seconds | Full driver/boot/userland/graphics/input/IPC/service/concurrency/fault suite plus new 4B target and 60-second soak with 42 cycles. |

### Resource Accounting

- QEMU mapped-exit workload: 1,025x1 RGB surface, 4,100 logical bytes, two
  PMM pages, mapped writable and intentionally left open on each process exit;
- warm-up: eight mapped exits; measured workload: twelve additional mapped
  exits;
- warmed and final state: zero processes, mappings, handles, mailboxes,
  services, shared-memory objects, and surfaces;
- warmed and final PMM free pages: `0x00006A29`;
- warmed and final heap: `used=0x003EDF70`, `mapped=0x003F3000`;
- transfer lifetime: sender handle release left one receiver reference; receiver
  release removed the object and both backing pages;
- unexplained resource drift: zero.

### Remaining

- the display present protocol, backend boundary, and sole normal `DISPLAY`
  authority belong to Phase 4C;
- ordinary compatibility launches still retain explicit full permissions;
  Phase 4 GUI clients use the new least-privilege named profile as their
  service pipeline is introduced;
- window policy, damage submission, composition, and input routing remain in
  Phases 4D through 4F.

## 4C: Display-Service Present Path

- Status: Complete
- Started: 2026-07-15
- Completed: 2026-07-15
- Implementation commit: `23ee0f0bd08f0c54c104fbe1f73304c5e673c365`

### Delivered

- added display ABI v1 and User SDK support for correlated
  `BEGIN -> DAMAGE chunks -> COMMIT -> REPLY` transactions with one
  read-only transferred surface, a maximum of 64 rectangles, exact accepted
  generations, stale rejection, bounded timeout cleanup, and newest-full-frame
  replacement of pending partial state;
- replaced the placeholder display service with a blocking IPC v2 server that
  validates sender PID plus process generation, request and frame generation,
  dimensions, stride, format, handle count/type/rights, chunk order, and commit
  totals before presentation;
- added an identity-safe service-owner lookup without changing the frozen
  `OsServiceInfo` v1 layout, plus generic received-object close and IPC v2
  blocking-wait SDK/syscall paths;
- added a stable kernel display backend boundary with GOP fallback, secondary
  clipping/merge validation, statistics, and a sole-normal-authority check that
  requires the currently registered `display` owner and attenuated
  `READ | MAP` surface rights;
- retained explicit direct-graphics diagnostic exceptions, while a restricted
  GUI application proved that ordinary direct display access is denied and
  service present succeeds;
- extended service supervision with a deterministic managed-child crash
  command, terminal fallback check, bounded automatic display restart, and
  post-restart frame resubmission;
- added host protocol/backend tests, real 800x600 pixel probes, full/partial
  ACK markers, permission checks, crash/restart coverage, resource checks, and
  public display-service documentation.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make clean && make -j2 uefi` | PASS | Clean kernel, SDK, userland, drivers, FAT32, and ESP build with zero compiler warnings. |
| `make test-display-present test-kernel-handles test-ipc-contracts test-service-registry test-abi-freeze` | PASS | Malformed/stale/duplicate/missing/reordered/oversized protocol cases, clipping/backend failures, handle attenuation, identity lookup, ABI freeze, and QEMU present path passed. |
| `make test-service-manager-smoke test-first-services` | PASS | Existing lifecycle, permissions, dependencies, health, bounded restart, and input/display service discovery remained intact. |
| `make test-closure` | PASS in 498.06 seconds | SDK 91/91; boot, driver, graphics, input, IPC, services, concurrency, fault injection, new 4C target, and 60-second soak with 41 cycles passed. |

### Resource Accounting

- QEMU producer surface: 800x600 BGR, 480,000 pixels, transferred as exactly
  `READ | MAP`; final screenshot contained 318,288 base-color pixels and
  30,000 partial-damage pixels despite concurrent terminal output;
- warmed sample before measured present:
  `processes=2 mappings=10 handles=0 mailboxes=0 services=2 shared=0 surfaces=0`;
- sample after present, forced display crash, automatic restart, and second
  present: the same active process/mapping/handle/mailbox/service/object counts,
  with heap unchanged at `used=0x001DAB70 mapped=0x001DF000`;
- active graphics-surface handles, mappings, objects, logical bytes, and backing
  pages returned to zero after every producer exit and after display restart;
- PMM samples include bounded returned-process image/page-table history until
  process-table slot reuse (`0x6C2B` through `0x6C3A` during this run), so the focused
  check excludes that explained history while requiring immediate transient
  surface cleanup; the warmed 60-second service soak completed 41 cycles with
  no reported resource drift;
- unexplained transient display-resource drift: zero.

### Remaining

- general multiwindow damage composition and input focus routing remain in
  Phases 4E and 4F;
- the explicit direct-graphics diagnostic exceptions remain until their tests
  are migrated to the complete service pipeline.

## 4D: Single-Window Bring-Up

- Status: Complete
- Started: 2026-07-15
- Completed: 2026-07-15
- Implementation commit: `231f27ed90322bc6c40d8a6c6801d88ddb463212`

### Delivered

- added the supervised `windowd_c.elf` service with a registered `window`
  identity, a dependency on `display`, and a GUI-service permission profile
  that has no direct `DISPLAY` authority;
- added a bounded window ABI and split server implementation for protocol
  validation, single-window state, opaque full-screen composition, and the
  input-routing boundary reserved for Phase 4F;
- implemented one-client `CREATE -> SET_SURFACE -> DAMAGE -> DESTROY` with
  exact PID/process-generation ownership, window/content generations,
  correlated replies, read-only surface transfer, and full-frame damage;
- made surface replacement transactional across display acknowledgement,
  rejected stale, malformed, and wrong-owner requests, and retained the last
  composed frame while cleaning an unexpectedly exited owner;
- tracked display-service identity changes and marked the retained full frame
  for resubmission after reconnect; the source/host contract verifies this
  state path, while display crash/restart behavior remains exercised by the
  existing display-service QEMU smoke;
- added a restricted deterministic window client, real pixel probes, lifecycle
  and owner-exit QEMU coverage, host protocol/state/compositor tests, service
  integration, and full regression wiring;
- expanded bounded execution slots safely, fixed higher-slot address
  calculation, reaped orphaned returned children, and allowed the FAT32 root
  directory to span multiple clusters for the additional service image.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make clean && make -j2 uefi` | PASS | Clean kernel, SDK, userland, drivers, FAT32, and ESP build with zero compiler warnings. |
| `make test-window-single` | PASS | Host protocol/state/compositor and authority checks plus supervised QEMU lifecycle, pixel, and unexpected-owner-exit cleanup passed. |
| `make test-display-present test-process-lifecycle test-ipc-contracts test-abi-freeze` | PASS | Earlier display restart, process cleanup, IPC rights, and frozen ABI contracts remained intact. |
| `make test-service-manager-smoke test-first-services` | PASS | Supervision, dependency, permission, health, restart, and existing input/display discovery behavior remained intact. |
| `make test-closure` | PASS in 515.74 seconds | SDK 91/91; boot, driver, graphics, input, IPC, services, concurrency, fault injection, Phase 4D, and 60-second service soak with 42 cycles passed. |

### Resource Accounting

- active supervised baseline and every post-client sample matched at
  `processes=4 mappings=21 handles=1 mailboxes=0 services=4 shared=0 surfaces=1`;
- the remaining handle and surface are the long-lived `windowd` composition
  surface; every transferred client surface, client mapping, and client handle
  was released after normal destroy and unexpected owner exit;
- the deterministic 800x600 frame contained 245,904 base-color pixels,
  120,000 secondary-color pixels, and 30,000 damage-highlight pixels despite
  concurrent terminal output;
- three complete measured client lifecycles covered unexpected exit, normal
  destroy, and a second unexpected exit with identical warmed/final resource
  samples;
- the aggregate 60-second service soak completed 42 cycles without reported
  drift;
- unexplained transient window-resource drift: zero.

### Remaining

- multiple simultaneous windows, bounded z-order, per-window position and
  clipping, damage accumulation, and selective recomposition are Phase 4E;
- keyboard/pointer forwarding, focus transitions, and stale-focus rejection
  remain Phase 4F;
- the public high-level window SDK and first event-driven GUI application are
  Phase 4G work;
- dedicated QEMU `windowd` crash/reconnect fault injection and GUI soak closure
  remain Phase 4H gates; Phase 4D does not claim that scenario as tested.

## Phase Milestones

The progress ledger is intentionally more detailed than project history. Add a
history page and annotated Git tag only for durable milestones such as the
first window, first complete GUI application, and Phase 4 closure. Routine
subphase completion remains recorded here and in Git commits.
