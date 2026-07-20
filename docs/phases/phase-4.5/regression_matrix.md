# Phase 4.5 Regression Matrix

This matrix defines the evidence required to close the threading foundation.
Rows P45-R01 through P45-R08 completed on 2026-07-20. Target names still marked
`reserved` are design names only: they must not be cited as executed evidence
until the Makefile contains them and they pass.

The final aggregate target will be `make test-phase45`. It must not be added as
an empty or documentation-only target.

## Matrix

| ID | Subphase | Contract | Planned automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P45-R01 | 4.5A | Thread records have bounded capacity, generation identities, legal transitions, and exactly-once cleanup. | `make test-thread-model` | Capacity failure rolls back; stale identities fail; slot reuse changes generation; terminal threads cannot be queued or resumed. | Complete |
| P45-R02 | 4.5B | Existing processes run through one extracted main thread with no duplicated execution-state authority. | `make test-thread-main`; `make test-phase4` | Boot, shell, services, GUI, preemption, waits, faults, and cleanup remain compatible with one main thread per process. | Complete |
| P45-R03 | 4.5C | Public create/current/exit/join/yield/sleep operations validate arguments and preserve stack and result lifetime. | `make test-thread-abi`; `make test-user-sdk` | Multiple threads execute and join; invalid, stale, cross-process, exhausted, and double-join cases fail safely; stack guards and rollback pass. | Complete |
| P45-R04 | 4.5D | Timer, child, IPC, input, key, character, and join waits block only the calling thread. | `make test-thread-waits` | Siblings progress; signal/timeout/cancel/exit races complete once; no wake is lost or duplicated. | Complete |
| P45-R05 | 4.5E | Mutex ownership and semaphore counts remain correct under contention and teardown. | `make test-thread-sync` | Non-owner unlock and overflow fail; blocked contenders wake according to policy; owner exit/object close cannot leak or deadlock. | Complete |
| P45-R06 | 4.5E | Condition variables and once initialization obey atomic wait and publication rules. | `make test-thread-sync` | Signal and broadcast are exact; wait returns with the mutex reacquired; timeout/cancel races and once failure/retry are deterministic. | Complete |
| P45-R07 | 4.5F | TLS, runtime accounting, and diagnostics belong to the selected thread. | `make test-thread-readiness` | TLS is isolated across switches; counters are monotonic; diagnostics use matching process/thread generations and do not expose stale state. | Complete |
| P45-R08 | 4.5F | Runnable threads, shell, and supervised GUI/service workloads satisfy a documented single-CPU progress bound. | `make test-thread-readiness`; `make test-drive-free-scheduler` | Mixed CPU-bound and blocking workloads make bounded progress without `drive`, busy polling, or starvation. | Complete |
| P45-R09 | 4.5G | Thread/process exit and fatal-fault policy release stacks, waits, objects, mappings, and shared resources exactly once. | Reserved: `make test-thread-faults` | Explicit thread exit is local; fatal shared-address-space faults identify the source thread and terminate/clean the process; injected failures leave zero drift. | Planned |
| P45-R10 | 4.5G | Repeated multithread lifecycle and synchronization churn is stable. | Reserved: `make test-thread-soak` | A minimum 60-second soak reports cycle counts and identical warmed/final resource snapshots with no terminal or stale queued thread. | Planned |
| P45-R11 | 4.5G | Phase 4.5 and every earlier contract pass together from a clean tree. | Future `make test-phase45`; existing `make test-closure`; clean parallel UEFI build | Every focused row and earlier suite passes; exact commands, results, durations, commits, and measurements are recorded. | Planned |

## Mandatory Negative Coverage

The focused targets must cover, where applicable:

- zero, malformed, stale, cross-process, and generation-mismatched identities;
- table, page, mapping, handle, wait-node, and object exhaustion;
- invalid entry points, stack sizes, flags, TLS bases, user destinations, and
  syscall pointers;
- self-join, second consuming join, non-owner unlock, semaphore overflow, and
  use after close;
- wake versus timeout, signal versus cancellation, join versus process exit,
  and thread creation versus process exit;
- terminal-thread queue insertion, duplicate wake, duplicate cleanup, and
  slot reuse while old references remain.

## Evidence Rules

- Replace `Reserved` with an exact real command only when the target exists.
- A host-only test cannot close a row that promises QEMU execution, timer
  preemption, user stack guards, fault handling, or end-to-end resource
  accounting.
- Each row needs both positive behavior and denial/failure behavior.
- Resource-sensitive tests record warmed and final counts, not only a PASS
  marker.
- A failed required command leaves the row `In progress`; it cannot be hidden
  by a later unrelated passing suite.
- P45-R01 through P45-R10 all block P45-R11 and Phase 4.5 closure.
- The optional one-hour release soak is useful release evidence but is not a
  substitute for, or prerequisite of, the required repeatable 60-second soak.
