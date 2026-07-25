# Phase 4.5: Threading Foundation

Phase 4.5 separates a schedulable thread from the process that owns an address
space and resources. The first implementation remains single-CPU. Its purpose
is to make multithreaded programs correct before Phase 4.6 permits multiple
CPUs to execute kernel and user code concurrently.

## Documents

- [Entry baseline](entry_baseline.md): the exact Phase 4 exit state and the
  process/scheduler limitations inherited by this phase.
- [Implementation plan](implementation_plan.md): subphases 4.5A through 4.5G,
  architectural decisions, ordering, and exit gates.
- [Regression matrix](regression_matrix.md): required contract coverage and
  reserved future test-target names.
- [Progress ledger](progress.md): live status and immutable evidence records.
- [Scheduler modernization plan](../../architecture/scheduler_modernization.md):
  the larger Phase 4H -> 4.5 -> 4.6 scheduling sequence.

## Current Status

Phase 4.5 completed on 2026-07-25. The kernel now has bounded
generation-tagged thread records, per-thread waits, mutex/semaphore/condition
objects, FS-based TLS, priority quanta, attributable scheduling counters, and
deterministic thread-allocation fault injection. User SDK 2.3 exposes
lifecycle, synchronization, TLS, priority, and thread diagnostic APIs.

Focused QEMU tests cover lifecycle reuse, exact wake policy, synchronization
teardown, TLS isolation, bounded CPU-sibling progress, fatal sibling-fault
attribution, and exact rollback at six thread-specific failure points. The
required 60-second thread/GUI/service churn soak, clean `make test-phase45`,
and full `make test-closure` all pass. Phase 4.6 SMP work may now begin.

## Scope Boundary

Phase 4.5 includes single-CPU user threads, per-thread waits and stacks,
correctness-first synchronization, thread-local storage, diagnostics, fault
handling, and stress closure.

It does not include application-processor startup, simultaneous execution on
multiple CPUs, work stealing, real-time scheduling classes, a desktop shell,
or a separate compositor process. Those remain Phase 4.6 or later work.
