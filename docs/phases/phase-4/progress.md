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
| 4B: Surface ABI, mapping, and transfer rights | Planned | - | - | - | - |
| 4C: Display-service present path | Planned | - | - | - | - |
| 4D: Single-window bring-up | Planned | - | - | - | - |
| 4E: Multiwindow compositor | Planned | - | - | - | - |
| 4F: Input routing and focus | Planned | - | - | - | - |
| 4G: Window SDK and first GUI application | Planned | - | - | - | - |
| 4H: Lifecycle, fault, regression, and closure | Planned | - | - | - | - |

Next planned work: Phase 4B, surface ABI, mapping, and transfer rights.

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

## Phase Milestones

The progress ledger is intentionally more detailed than project history. Add a
history page and annotated Git tag only for durable milestones such as the
first window, first complete GUI application, and Phase 4 closure. Routine
subphase completion remains recorded here and in Git commits.
