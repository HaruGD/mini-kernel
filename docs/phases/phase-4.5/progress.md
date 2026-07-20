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
| 4.5D: Thread-aware waiting | Planned | - | - | - | - |
| 4.5E: Synchronization primitives | Planned | - | - | - | - |
| 4.5F: TLS, accounting, and fairness | Planned | - | - | - | - |
| 4.5G: Fault injection, soak, and closure | Planned | - | - | - | - |

Current status: 4.5A through 4.5C are complete. The next task is 4.5D
thread-aware waiting. Phase 4.6 and Phase 5 remain gated on Phase 4.5 closure.

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

4.5D must add and certify multiple eligible waiters per process. Current
storage is per-thread, but IPC/input wake selection and timeout/cancel races
still need the focused tests required by P45-R04.
