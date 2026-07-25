# Phase 4.5 Progress

This is the live implementation ledger for the threading foundation. Stable
design belongs in [implementation_plan.md](implementation_plan.md); required
coverage belongs in [regression_matrix.md](regression_matrix.md). This file
records only work that has actually started or completed.

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but the exit gate has not passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: every exit-gate requirement passed and an evidence record exists.

A documentation plan is not implementation completion. `Complete` requires an
implementation commit, exact verification commands and results, resource
measurements where applicable, and a separate evidence commit that records the
immutable implementation hash.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4.5A: Thread object model | Complete | 2026-07-20 | 2026-07-20 | `63cb234` | Thread model host tests pass |
| 4.5B: Main thread extraction | Complete | 2026-07-20 | 2026-07-20 | `63cb234` | Full Phase 4 suite passes |
| 4.5C: Thread ABI and SDK | Complete | 2026-07-20 | 2026-07-20 | `63cb234` | SDK 2.2 QEMU lifecycle and reuse pass |
| 4.5D: Thread-aware waiting | Complete | 2026-07-20 | 2026-07-20 | `cdb92ca` | Wait model and QEMU input/IPC regressions pass |
| 4.5E: Synchronization primitives | Complete | 2026-07-20 | 2026-07-20 | `2211643` | QEMU synchronization policy test passes |
| 4.5F: TLS, accounting, and fairness | Complete | 2026-07-20 | 2026-07-20 | `1594f34` | QEMU TLS/accounting/fairness test passes |
| 4.5G: Fault injection, soak, and closure | Complete | 2026-07-25 | 2026-07-25 | `9acb245` | Six injected failure paths, fatal sibling fault, 60-second soak, aggregate and full closure pass |

Current status: Phase 4.5 is complete. All eleven matrix rows pass from the
immutable implementation commit `9acb245`; Phase 4.6 SMP work may begin.

## Recording Workflow

For each subphase:

1. set the row to `In progress` when code or tests begin;
2. implement the bounded behavior and focused positive/negative tests;
3. run the focused exit-gate commands and all affected earlier suites;
4. commit implementation and tests;
5. add a dated record below using that immutable implementation commit;
6. change the row and matching roadmap item to `Complete` only after every
   required result passes;
7. commit the evidence update separately.

The evidence commit is separate because a Git commit cannot contain its own
final hash. Do not record a predicted hash.

## Evidence Record Format

Append one section per completed subphase:

```text
## 4.5X: Subphase Name

Status: Complete
Started: YYYY-MM-DD
Completed: YYYY-MM-DD
Implementation commits: `<immutable commit hashes>`

### Delivered

- concrete object, ABI, scheduler, wait, or synchronization result;
- compatibility and failure-policy result;
- diagnostics and tests delivered.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <real-focused-target>` | PASS | counts, timing, or QEMU markers |
| `make <affected-regression>` | PASS | exact result |

### Resource Accounting

- warmed baseline: thread records/stacks/pages/handles/waits/objects/processes;
- final sample: same tuple and sampling boundary;
- unexplained drift: zero or a documented blocker.

### Remaining

- work assigned to the next subphase;
- accepted limitation already named in the plan.
```

## Phase 4 Entry Evidence

Phase 4 is the immutable starting point, not Phase 4.5 completion evidence.
It closed on 2026-07-20 with implementation commits `31b681e` and `8d44929`,
documentation commit `064e835`, and tag `phase-4-complete`. The aggregate Phase
4 test and full closure passed, including the required 60-second GUI soak with
zero warmed/final resource drift.

## 4.5A: Thread Object Model

- Status: Complete
- Started: 2026-07-20
- Completed: 2026-07-20
- Implementation commit: `63cb234`

### Delivered

- Added a bounded 32-record global thread table with a maximum of four threads
  per process and nonzero TID-plus-generation identities.
- Added one authoritative `ThreadContext`, owner PID/generation validation,
  bounded allocation, terminal state, join-result retention, and safe slot
  reuse.
- Extended scheduler diagnostics with total thread count and generation-tagged
  queue entries without exposing kernel addresses.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-thread-model` | PASS | Capacity denial, self/cross-process join denial, identity reuse, stale lookup rejection, and cleanup passed. |
| `make test-thread-main` | PASS | Thread model, process lifecycle, and ABI/main-extraction contracts passed. |

### Resource Accounting

The host model fills all four per-process records, denies a fifth, clears the
owner, rejects the old identity, and successfully publishes a new generation.
No live host thread record remains after the final `process_clear`.

## 4.5B: Main Thread Extraction

- Status: Complete
- Started: 2026-07-20
- Completed: 2026-07-20
- Implementation commit: `63cb234`

### Delivered

- Every process is constructed with one main `Thread`; process-wide resources
  remain in `Process`, while registers, stacks, wait state, and scheduling
  accounting have one `ThreadContext` authority.
- The ready queue, timer preemption, yield/sleep/wait paths, current-context
  accessors, and resume path select a `Thread*` and derive its owning process.
- Every thread has a private page-backed Ring 0 entry stack, and TSS `RSP0` is
  changed with the selected thread.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-phase4` | PASS | 222.5 seconds; all surface, display, window, input, GUI, fault-injection, and recovery tests passed. |
| `make test-drive-free-scheduler` | PASS | Static scheduler contract and QEMU service-control path passed without `drive`. |
| `make test-user-sdk` | PASS | 91/91 existing SDK checks passed. |

### Resource Accounting

The 60-second GUI soak completed 33 application cycles, 132 window cycles, and
six GUI service-stack restarts. Warmed and final tuples were identical:
`(4,21,1,0,4,0,1,27168,1944432,1961984)`.

## 4.5C: Thread ABI And SDK

- Status: Complete
- Started: 2026-07-20
- Completed: 2026-07-20
- Implementation commit: `63cb234`

### Delivered

- Froze thread ABI v1 with an 8-byte identity and 40-byte create request, plus
  syscall numbers 89 through 92 for create, self, exit, and join.
- Added User SDK 2.2 wrappers for create, self, exit, join, yield, and sleep;
  the SDK return trampoline converts a thread-entry return value into exit.
- Added guarded zeroed user stacks, private kernel stacks, validated executable
  entry/trampoline addresses, known-flag checks, bounded capacity, and
  deterministic self/stale/cross-process/double-join denial.
- Added `uthread_c.elf`, which runs three siblings through yield and sleep,
  joins statuses 64/65/66, tests exhaustion, consumes results once, and reuses
  a released slot.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-thread-abi` | PASS | Ten complete QEMU lifecycle runs; every run reported counts 12/12/12, statuses 64/65/66, and `failures=0`. |
| `make test-phase45-abc` | PASS | Thread model/ABI, User SDK 91/91, and drive-free scheduler/service control passed together. |

### Resource Accounting

After warming the bounded eight-process result table, the QEMU thread test
recorded identical resource tuples before and after another full lifecycle:
`(0,0,0,0,0,0,0,27186,4120432,4141056)`. Scheduler snapshots were also
identical at `(ready queue=0, thread records=8)`. The retained eight terminal
main-thread records are the bounded process-result history, not live or queued
threads.

### Remaining

Completed by 4.5D through 4.5F. Phase 4.5G owns injected allocation failures,
fatal sibling-fault cleanup, the required 60-second churn soak, and aggregate
closure.

## 4.5D: Thread-Aware Waiting

- Status: Complete
- Started: 2026-07-20
- Completed: 2026-07-20
- Implementation commit: `cdb92ca`

### Delivered

- Added generation-owned per-thread wait sequences and oldest-waiter
  selection for process-wide IPC and input storage.
- Defined one-item/one-waiter input wake priority and broadcast cancellation.
- Closed IPC, input, key, and character check-to-wait races with a final queue
  recheck after arming the caller's wait.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-thread-waits` | PASS | Host wait ordering/cancellation/timeouts passed; ten QEMU lifecycle runs retained an empty ready queue and the same warmed/final resource tuple. |
| `make test-ipc-smoke` | PASS | Blocking IPC wake path passed. |
| `make test-input-event-loop` | PASS | Blocking input delivery passed. |
| `make test-window-input` | PASS | Focus routing and GUI input regression passed. |

## 4.5E: Synchronization Primitives

- Status: Complete
- Started: 2026-07-20
- Completed: 2026-07-20
- Implementation commit: `2211643`

### Delivered

- Added bounded generation-tagged mutex, semaphore, and condition handle
  objects plus User SDK APIs and blocking once initialization.
- Enforced non-recursive mutex ownership, non-owner denial, semaphore maximum
  counts, exact waiter wakes, condition mutex reacquisition, finite timeouts,
  broadcast, close cancellation, and owner-exit recovery.
- Kept wait queues in the scheduler wait core and removed every object owner
  and waiter reference during close or process teardown.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-thread-sync` | PASS | QEMU reported `counter=120 once=1 failures=0`; mutex, semaphore, condition, owner-exit, timeout, overflow, broadcast, and close-cancel sections all reported zero failures. |
| `make test-thread-waits test-spinlocks test-abi-freeze` | PASS | Earlier wait ordering, lock discipline, and frozen ABI checks remained green. |

## 4.5F: TLS, Accounting, And Fairness

- Status: Complete
- Started: 2026-07-20
- Completed: 2026-07-20
- Implementation commit: `1594f34`

### Delivered

- Added validated per-thread TLS base setters/getters and applies IA32_FS_BASE
  both on the setting syscall return and every later thread resume.
- Added 64-bit runtime, preemption, yield, block, wake, and switch counters,
  process aggregates derived from thread records, and public generation-safe
  thread diagnostics.
- Added low/normal/high FIFO priority quanta of 4/6/8 ticks while retaining a
  bounded single-CPU ready queue and no busy-polling wait path.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-thread-readiness` | PASS | `%fs:0` stayed isolated; work counts were `26444/25868/26258`, preemptions `7/5/1`, and failures `0`. |
| `make test-drive-free-scheduler` | PASS | Shell and supervised service control retained bounded progress without `drive`. |
| `make test-thread-sync test-thread-waits test-abi-freeze` | PASS | Synchronization, wait lifecycle, and ABI regressions remained green. |

### Remaining

Completed by 4.5G. Phase 4.6 owns application-processor startup, per-CPU state,
cross-CPU scheduling, IPIs, TLB shootdown, and the SMP lock audit.

## 4.5G: Fault Injection, Soak, And Closure

- Status: Complete
- Started: 2026-07-25
- Completed: 2026-07-25
- Implementation commit: `9acb245`

### Delivered

- Added deterministic `thread_record`, `thread_user_stack`,
  `thread_kernel_stack`, `thread_mapping`, `thread_wait`, and `sync_object`
  failure points. Each injected failure is consumed once and rolls back to the
  same warmed process, mapping, handle, mailbox, service, shared-object,
  surface, PMM, and heap snapshot.
- Added a multithreaded fatal-fault application whose first sibling remains
  blocked while a second sibling triggers a user page fault. Diagnostics retain
  the faulting TID and generation, the shared-address-space policy terminates
  the process, and no sibling returns to user mode.
- Fixed process-wide termination to release every non-current sibling runtime
  stack exactly once. The new fault test found the original four-page drift
  before the fix and proves zero drift afterward.
- Added the required thread/synchronization/GUI/service churn soak and the real
  `make test-phase45` aggregate target.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-thread-faults` | PASS | Six thread-specific injected failures fired exactly once; fatal sibling page fault reported its thread identity; scheduler ready queue and all lock-violation counters ended at zero. |
| `make test-thread-soak` | PASS | 60 seconds, 14 composite cycles, 42 thread programs, and 56 GUI window lifecycles. |
| `make clean` followed by `make -j2 uefi uefi-diagnostic` | PASS | Both normal and diagnostic UEFI images rebuilt from an empty build/output state. |
| `make test-phase45` | PASS | Thread model, ABI/SDK 91/91, drive-free scheduling, waits, synchronization, readiness, both fault suites, and the required 60-second soak passed together. |
| `make test-closure` | PASS | All earlier driver, UEFI, userland, graphics, input, IPC, service, concurrency, fault, and Phase 4 suites passed; GUI soak completed 35 cycles/140 windows/7 restarts and service soak completed 42 cycles. |

### Resource Accounting

The thread fault run recorded identical warmed and final tuples:
`(0,0,0,0,0,0,0,27226,4096000,4116480)`. The tuple is processes, mappings,
handles, mailboxes, services, shared objects, surfaces, free PMM pages, heap
bytes used, and heap bytes mapped.

The required 60-second combined soak recorded identical warmed and final
tuples: `(4,21,1,0,4,0,1,27183,1944432,1961984)`. It also ended with no ready
thread, stale terminal queue entry, kernel panic, double fault, or spinlock
order/recursion/release violation. Unexplained drift is zero.

### Remaining

Phase 4.5 has no open exit-gate item. The optional one-hour release soak
remains useful release evidence but is not required for Phase 4.6 entry.
